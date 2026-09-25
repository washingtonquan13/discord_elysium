#include "VoiceClient.h"

#include "AudioEngine.h"
#include "DaveSession.h"
#include "VoiceUdp.h"
#include "core/Http.h"
#include "core/Json.h"
#include "core/Settings.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>

#include <mutex>

namespace kestrel {

using namespace json;

namespace {
enum Op {
    Identify = 0, SelectProtocol = 1, Ready = 2, Heartbeat = 3, SessionDescription = 4, Speaking = 5, HeartbeatAck = 6,
    Hello = 8, ClientConnect = 11, ClientDisconnect = 13, SessionUpdate = 14,
    DavePrepareTransition = 21, DaveExecuteTransition = 22, DaveReadyForTransition = 23, DavePrepareEpoch = 24,
    MlsExternalSender = 25, MlsKeyPackage = 26, MlsProposals = 27, MlsCommitWelcome = 28, MlsAnnounceCommit = 29,
    MlsWelcome = 30, MlsInvalidCommitWelcome = 31,
};
const char *kMode = "aead_xchacha20_poly1305_rtpsize";
std::mutex g_pipeMutex; // guards m_dave / m_udp use from audio + network threads
} // namespace

VoiceClient::VoiceClient(AudioEngine *audio, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_audio(audio)
    , m_settings(settings) {
    connect(&m_heartbeat, &QTimer::timeout, this, [this]() {
        m_lastHeartbeat = QDateTime::currentMSecsSinceEpoch();
        sendJson(Heartbeat, QJsonObject { { "t", m_lastHeartbeat }, { "seq_ack", m_seq } });
    });
    m_keepalive.setInterval(5000);
    connect(&m_keepalive, &QTimer::timeout, this, [this]() {
        std::lock_guard lock(g_pipeMutex);
        if (m_udp) m_udp->keepalive();
    });

    m_audio->onEncoded = [this](const uint8_t *data, size_t len) {
        std::lock_guard lock(g_pipeMutex);
        if (!m_udp) return;
        const bool silence = len == 3 && data[0] == 0xF8 && data[1] == 0xFF && data[2] == 0xFE;
        if (m_dave && m_dave->enabled() && !silence) {
            std::vector<uint8_t> out;
            if (m_dave->encrypt(data, len, out)) m_udp->sendOpus(out.data(), out.size());
            return;
        }
        m_udp->sendOpus(data, len);
    };
    m_audio->onSpeakingChanged = [this](bool speaking) {
        QMetaObject::invokeMethod(this, [this, speaking]() {
            if (m_state == "connected")
                sendJson(Speaking, QJsonObject { { "speaking", speaking ? 1 : 0 }, { "delay", 0 }, { "ssrc", static_cast<qint64>(m_ssrc) } });
        }, Qt::QueuedConnection);
    };
}

VoiceClient::~VoiceClient() {
    disconnectVoice();
    m_audio->onEncoded = nullptr;
    m_audio->onSpeakingChanged = nullptr;
}

// Detach the media pipeline under the lock, then destroy it outside the lock:
// VoiceUdp::close() joins the receive thread, which may itself be waiting on
// g_pipeMutex inside onAudio.
void VoiceClient::teardownPipe() {
    std::unique_ptr<VoiceUdp> udp;
    std::unique_ptr<DaveSession> dave;
    {
        std::lock_guard lock(g_pipeMutex);
        udp.swap(m_udp);
        dave.swap(m_dave);
    }
    if (udp) udp->close();
    udp.reset();
    dave.reset();
}

void VoiceClient::setState(const QString &s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

void VoiceClient::setSession(Snowflake userId, const QString &sessionId) {
    m_userId = userId;
    const bool changed = sessionId != m_sessionId;
    m_sessionId = sessionId;
    if (changed) tryConnect();
}

void VoiceClient::setServer(Snowflake guildId, Snowflake channelId, const QString &endpoint, const QString &token) {
    m_guildId = guildId;
    m_channelId = channelId;
    m_endpoint = endpoint;
    m_token = token;
    m_reconnects = 0;
    if (endpoint.isEmpty()) {
        // server is being allocated; a new VOICE_SERVER_UPDATE will follow
        setState("connecting");
        return;
    }
    tryConnect();
}

void VoiceClient::tryConnect() {
    if (m_sessionId.isEmpty() || m_endpoint.isEmpty() || m_token.isEmpty() || !m_channelId) return;
    // tear down any previous connection (region change / move)
    if (m_ws) {
        m_ws->disconnect(this);
        m_ws->abort();
        m_ws->deleteLater();
        m_ws = nullptr;
    }
    teardownPipe();
    m_audio->stop();
    m_ssrcUsers.clear();
    m_connectedUsers.clear();
    m_seq = -1;
    m_daveActive = false;
    emit encryptionChanged(false);
    setState("connecting");

    QString host = m_endpoint;
    if (host.startsWith("wss://")) host = host.mid(6);
    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &VoiceClient::onText);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &VoiceClient::onBinary);
    connect(m_ws, &QWebSocket::disconnected, this, [this]() {
        const int code = m_ws ? m_ws->closeCode() : 0;
        qInfo() << "Voice websocket closed:" << code << (m_ws ? m_ws->closeReason() : QString());
        m_heartbeat.stop();
        m_keepalive.stop();
        if (m_state == "disconnected") return;
        // 4014: we were disconnected/moved (the main gateway tells us what's next)
        if (code == 4014) {
            // disconnected or moved: the main gateway sends a new server update if we're still in a channel
            setState("connecting");
            QTimer::singleShot(8000, this, [this]() {
                if (m_state == "connecting" && (!m_ws || m_ws->state() != QAbstractSocket::ConnectedState)) setState("error");
            });
            return;
        }
        if (code == 4004 || code == 4006 || m_reconnects >= 3) {
            setState("error");
            return;
        }
        m_reconnects++;
        QTimer::singleShot(1500, this, &VoiceClient::tryConnect);
    });
    const QString scheme = qEnvironmentVariable("KESTREL_VOICE_SCHEME", "wss");
    QNetworkRequest req { QUrl(scheme + "://" + host + "/?v=8") };
    req.setHeader(QNetworkRequest::UserAgentHeader, Http::userAgent());
    req.setRawHeader("Origin", "https://discord.com");
    m_ws->open(req);
}

void VoiceClient::disconnectVoice() {
    setState("disconnected");
    m_heartbeat.stop();
    m_keepalive.stop();
    if (m_ws) {
        m_ws->disconnect(this);
        m_ws->close();
        m_ws->deleteLater();
        m_ws = nullptr;
    }
    teardownPipe();
    m_audio->stop();
    m_channelId = 0;
    m_endpoint.clear();
    m_token.clear();
    m_sessionId.clear();
    m_ssrcUsers.clear();
    m_connectedUsers.clear();
    m_daveActive = false;
}

void VoiceClient::sendJson(int op, const QJsonValue &d) {
    if (!m_ws || m_ws->state() != QAbstractSocket::ConnectedState) return;
    m_ws->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject { { "op", op }, { "d", d } }).toJson(QJsonDocument::Compact)));
}

void VoiceClient::sendBinary(int op, const std::vector<uint8_t> &payload) {
    if (!m_ws) return;
    QByteArray frame;
    frame.reserve(static_cast<int>(payload.size()) + 1);
    frame.append(static_cast<char>(op));
    frame.append(reinterpret_cast<const char *>(payload.data()), static_cast<int>(payload.size()));
    m_ws->sendBinaryMessage(frame);
}

void VoiceClient::mapUser(uint32_t ssrc, Snowflake userId) {
    if (!ssrc || !userId) return;
    m_ssrcUsers.insert(ssrc, userId);
    m_connectedUsers.insert(QString::number(userId));
    m_audio->setSourceVolume(ssrc, static_cast<float>(m_settings->userVolume(QString::number(userId))));
    std::lock_guard lock(g_pipeMutex);
    if (m_dave) {
        m_dave->addUser(std::to_string(userId));
        m_dave->mapSsrc(ssrc, userId);
    }
}

void VoiceClient::setUserVolume(Snowflake userId, float volume) {
    for (auto it = m_ssrcUsers.begin(); it != m_ssrcUsers.end(); ++it)
        if (it.value() == userId) m_audio->setSourceVolume(it.key(), volume);
}

QSet<Snowflake> VoiceClient::speakingUsers() {
    QSet<Snowflake> out;
    if (m_state != "connected") return out;
    for (auto ssrc : m_audio->activeSources())
        if (auto it = m_ssrcUsers.find(ssrc); it != m_ssrcUsers.end()) out.insert(it.value());
    if (m_audio->transmitting()) out.insert(m_userId);
    return out;
}

void VoiceClient::ensureDave(int version) {
    std::lock_guard lock(g_pipeMutex);
    if (m_dave || version <= 0) return;
    m_dave = std::make_unique<DaveSession>(m_channelId, m_userId);
    m_dave->sendBinary = [this](int op, const std::vector<uint8_t> &p) { sendBinary(op, p); };
    m_dave->sendReadyForTransition = [this](int id) { sendJson(DaveReadyForTransition, QJsonObject { { "transition_id", id } }); };
    m_dave->sendInvalidCommitWelcome = [this](int id) { sendJson(MlsInvalidCommitWelcome, QJsonObject { { "transition_id", id } }); };
    m_dave->stateChanged = [this](bool on) {
        m_daveActive = on;
        QMetaObject::invokeMethod(this, [this, on]() { emit encryptionChanged(on); }, Qt::QueuedConnection);
    };
    m_dave->setLocalSsrc(m_ssrc);
    for (const auto &u : m_connectedUsers) m_dave->addUser(u.toStdString());
    for (auto it = m_ssrcUsers.begin(); it != m_ssrcUsers.end(); ++it) m_dave->mapSsrc(it.key(), it.value());
    m_dave->init(static_cast<uint16_t>(version));
}

void VoiceClient::onText(const QString &text) {
    const auto msg = QJsonDocument::fromJson(text.toUtf8()).object();
    if (!isNull(msg, "seq")) m_seq = i32(msg, "seq", m_seq);
    const int op = i32(msg, "op", -1);
    const auto d = obj(msg, "d");

    switch (op) {
        case Hello:
            m_heartbeat.start(static_cast<int>(qMax<qint64>(1000, i64(d, "heartbeat_interval", 13750))));
            sendJson(Identify, QJsonObject {
                                   { "server_id", idStr(m_guildId ? m_guildId : m_channelId) },
                                   { "channel_id", idStr(m_channelId) },
                                   { "user_id", idStr(m_userId) },
                                   { "session_id", m_sessionId },
                                   { "token", m_token },
                                   { "video", true },
                                   { "streams", QJsonArray { QJsonObject { { "type", "video" }, { "rid", "100" }, { "quality", 100 } } } },
                                   { "max_dave_protocol_version", 1 },
                               });
            break;
        case Ready: {
            m_ssrc = static_cast<uint32_t>(i64(d, "ssrc"));
            const QString ip = str(d, "ip");
            const int port = i32(d, "port");
            bool modeOk = false;
            for (const auto &m : arr(d, "modes")) modeOk |= m.toString() == kMode;
            if (!modeOk) qWarning() << "Voice server does not offer" << kMode;

            auto udp = std::make_unique<VoiceUdp>();
            udp->setSsrc(m_ssrc);
            udp->onDiscovered = [this](const std::string &addr, uint16_t p) {
                QMetaObject::invokeMethod(this, [this, addr, p]() {
                    const QString a = QString::fromStdString(addr);
                    sendJson(SelectProtocol, QJsonObject {
                                                 { "protocol", "udp" },
                                                 { "address", a },
                                                 { "port", p },
                                                 { "mode", kMode },
                                                 { "data", QJsonObject { { "address", a }, { "port", p }, { "mode", kMode } } },
                                             });
                }, Qt::QueuedConnection);
            };
            udp->onAudio = [this](uint32_t ssrc, const uint8_t *data, size_t len) {
                std::vector<uint8_t> plain;
                {
                    std::lock_guard lock(g_pipeMutex);
                    const bool silence = len == 3 && data[0] == 0xF8 && data[1] == 0xFF && data[2] == 0xFE;
                    if (m_dave && !silence) {
                        if (m_dave->enabled()) {
                            if (!m_dave->decrypt(ssrc, data, len, plain)) return;
                            data = plain.data();
                            len = plain.size();
                        } else if (!m_dave->downgraded()) {
                            return; // E2EE pending: can't decode yet
                        }
                    }
                }
                m_audio->feedOpus(ssrc, data, len);
            };
            if (!udp->open(ip.toStdString(), static_cast<uint16_t>(port))) {
                setState("error");
                return;
            }
            {
                std::lock_guard lock(g_pipeMutex);
                m_udp = std::move(udp);
            }
            // retry discovery a few times; UDP can drop the first datagram
            for (int i = 0; i < 3; i++) {
                QTimer::singleShot(i * 700, this, [this]() {
                    std::lock_guard lock(g_pipeMutex);
                    if (m_udp && m_state != "connected") m_udp->discover(m_ssrc);
                });
            }
            m_keepalive.start();
            break;
        }
        case SessionDescription: {
            std::array<uint8_t, 32> key {};
            const auto k = arr(d, "secret_key");
            for (int i = 0; i < 32 && i < k.size(); i++) key[i] = static_cast<uint8_t>(k[i].toInt());
            {
                std::lock_guard lock(g_pipeMutex);
                if (m_udp) m_udp->setKey(key);
            }
            ensureDave(i32(d, "dave_protocol_version"));
            m_audio->setPushToTalk(m_settings->pushToTalk(), m_settings->pushToTalkKey());
            m_audio->setVadThreshold(static_cast<float>(m_settings->vadThreshold()));
            m_audio->setInputGain(static_cast<float>(m_settings->inputVolume()));
            m_audio->setOutputGain(static_cast<float>(m_settings->outputVolume()));
            m_audio->start(m_settings->inputDevice().toStdString(), m_settings->outputDevice().toStdString());
            sendJson(Speaking, QJsonObject { { "speaking", 0 }, { "delay", 0 }, { "ssrc", static_cast<qint64>(m_ssrc) } });
            m_reconnects = 0;
            setState("connected");
            break;
        }
        case Speaking:
            mapUser(static_cast<uint32_t>(i64(d, "ssrc")), id(d, "user_id"));
            break;
        case HeartbeatAck:
            if (m_lastHeartbeat) m_ping = static_cast<int>(QDateTime::currentMSecsSinceEpoch() - m_lastHeartbeat);
            break;
        case ClientConnect:
            for (const auto &u : arr(d, "user_ids")) {
                m_connectedUsers.insert(u.toString());
                std::lock_guard lock(g_pipeMutex);
                if (m_dave) m_dave->addUser(u.toString().toStdString());
            }
            break;
        case ClientDisconnect: {
            const QString uid = str(d, "user_id");
            m_connectedUsers.remove(uid);
            for (auto it = m_ssrcUsers.begin(); it != m_ssrcUsers.end();) {
                if (QString::number(it.value()) == uid) {
                    m_audio->removeSource(it.key());
                    it = m_ssrcUsers.erase(it);
                } else {
                    ++it;
                }
            }
            std::lock_guard lock(g_pipeMutex);
            if (m_dave) m_dave->removeUser(uid.toStdString());
            break;
        }
        case SessionUpdate:
            if (has(d, "audio_ssrc")) mapUser(static_cast<uint32_t>(i64(d, "audio_ssrc")), id(d, "user_id"));
            break;
        case DavePrepareTransition: {
            std::lock_guard lock(g_pipeMutex);
            if (m_dave) m_dave->onPrepareTransition(i32(d, "protocol_version"), i32(d, "transition_id"));
            break;
        }
        case DaveExecuteTransition: {
            std::lock_guard lock(g_pipeMutex);
            if (m_dave) m_dave->onExecuteTransition(i32(d, "transition_id"));
            break;
        }
        case DavePrepareEpoch: {
            const int version = i32(d, "protocol_version");
            if (version > 0) ensureDave(version);
            std::lock_guard lock(g_pipeMutex);
            if (m_dave) m_dave->onPrepareEpoch(version, i32(d, "epoch"));
            break;
        }
        default:
            break;
    }
}

void VoiceClient::onBinary(const QByteArray &data) {
    // server -> client binary frames: [seq:u16be][opcode:u8][payload]
    if (data.size() < 3) return;
    const auto *raw = reinterpret_cast<const uint8_t *>(data.constData());
    m_seq = (raw[0] << 8) | raw[1];
    const int op = raw[2];
    const uint8_t *payload = raw + 3;
    const size_t size = static_cast<size_t>(data.size() - 3);

    std::lock_guard lock(g_pipeMutex);
    if (!m_dave) return;
    switch (op) {
        case MlsExternalSender: m_dave->onExternalSender(payload, size); break;
        case MlsProposals: m_dave->onProposals(payload, size); break;
        case MlsAnnounceCommit: m_dave->onCommitTransition(payload, size); break;
        case MlsWelcome: m_dave->onWelcome(payload, size); break;
        default: break;
    }
}

} // namespace kestrel
