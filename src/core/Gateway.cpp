#include "Gateway.h"

#include "Http.h"
#include "Json.h"

#include <QJsonDocument>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUrlQuery>

namespace kestrel {

using namespace json;

Gateway::Gateway(const QString &url, Http *http, QObject *parent)
    : QObject(parent)
    , m_url(url)
    , m_http(http) {
    m_heartbeat.setTimerType(Qt::CoarseTimer);
    connect(&m_heartbeat, &QTimer::timeout, this, &Gateway::sendHeartbeat);
    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, &Gateway::open);
}

Gateway::~Gateway() {
    m_wantConnected = false;
    if (m_ws) m_ws->abort();
    if (m_zsInit) inflateEnd(&m_zs);
}

void Gateway::resetInflate() {
    if (m_zsInit) inflateEnd(&m_zs);
    m_zs = {};
    inflateInit(&m_zs);
    m_zsInit = true;
    m_zbuf.clear();
}

void Gateway::connectWithToken(const QString &token) {
    m_token = token;
    m_wantConnected = true;
    m_canResume = false;
    m_resumeAttempts = 0;
    m_seq = -1;
    m_sessionId.clear();
    m_resumeUrl.clear();
    open();
}

void Gateway::disconnectFromHost() {
    m_wantConnected = false;
    m_ready = false;
    m_heartbeat.stop();
    m_reconnect.stop();
    if (m_ws) {
        m_ws->close(QWebSocketProtocol::CloseCodeNormal);
        m_ws->deleteLater();
        m_ws = nullptr;
    }
    emit stateChanged("disconnected");
}

void Gateway::open() {
    if (!m_wantConnected) return;
    if (m_ws) {
        m_ws->disconnect(this);
        m_ws->abort();
        m_ws->deleteLater();
    }
    resetInflate();
    m_ready = false;
    m_acked = true;

    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_ws->setMaxAllowedIncomingMessageSize(512 * 1024 * 1024);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &Gateway::onBinary);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &Gateway::onText);
    connect(m_ws, &QWebSocket::disconnected, this, [this]() {
        const int code = m_ws ? m_ws->closeCode() : 0;
        qInfo() << "Gateway closed:" << code << (m_ws ? m_ws->closeReason() : QString());
        m_heartbeat.stop();
        m_ready = false;
        if (!m_wantConnected) return;
        switch (code) {
            case 4004: // authentication failed
                m_wantConnected = false;
                emit authenticationFailed();
                return;
            case 4010: case 4011: case 4012: case 4013: case 4014:
                m_wantConnected = false;
                emit stateChanged("error");
                return;
            case 4007: case 4009: // invalid sequence / session timed out
                scheduleReconnect(false);
                return;
            default:
                scheduleReconnect(true);
        }
    });

    // After a couple of failed resumes, fall back to the main gateway with a fresh identify.
    if (m_canResume && m_resumeAttempts >= 2) {
        m_canResume = false;
        m_seq = -1;
        m_sessionId.clear();
    }
    const bool resuming = m_canResume && !m_resumeUrl.isEmpty();
    if (m_canResume) m_resumeAttempts++;
    QUrl url(resuming ? m_resumeUrl : m_url);
    if (resuming) {
        // resume_gateway_url comes without a path or query parameters
        url.setQuery(QUrlQuery(QUrl(m_url).query()));
    }
    if (url.path().isEmpty()) url.setPath("/");
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, Http::userAgent());
    req.setRawHeader("Origin", "https://discord.com");
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    emit stateChanged("connecting");
    m_ws->open(req);
}

void Gateway::scheduleReconnect(bool canResume) {
    m_canResume = canResume && !m_sessionId.isEmpty();
    if (!m_canResume) {
        m_seq = -1;
        m_sessionId.clear();
    }
    emit stateChanged("reconnecting");
    const int jitter = QRandomGenerator::global()->bounded(500);
    m_reconnect.start(m_backoff + jitter);
    m_backoff = qMin(m_backoff * 2, 60000);
}

void Gateway::onBinary(const QByteArray &data) {
    m_zbuf.append(data);
    if (m_zbuf.size() < 4 || !m_zbuf.endsWith(QByteArray("\x00\x00\xff\xff", 4))) return;

    QByteArray out;
    char chunk[64 * 1024];
    m_zs.next_in = reinterpret_cast<Bytef *>(m_zbuf.data());
    m_zs.avail_in = static_cast<uInt>(m_zbuf.size());
    do {
        m_zs.next_out = reinterpret_cast<Bytef *>(chunk);
        m_zs.avail_out = sizeof(chunk);
        const int err = inflate(&m_zs, Z_SYNC_FLUSH);
        if (err != Z_OK && err != Z_BUF_ERROR && err != Z_STREAM_END) {
            qWarning() << "Gateway inflate error" << err;
            m_zbuf.clear();
            if (m_ws) m_ws->close(QWebSocketProtocol::CloseCode(4000));
            return;
        }
        out.append(chunk, static_cast<int>(sizeof(chunk) - m_zs.avail_out));
    } while (m_zs.avail_out == 0);
    m_zbuf.clear();

    QJsonParseError perr;
    const auto doc = QJsonDocument::fromJson(out, &perr);
    if (perr.error != QJsonParseError::NoError) {
        qWarning() << "Gateway JSON error:" << perr.errorString();
        return;
    }
    handle(doc.object());
}

void Gateway::onText(const QString &text) {
    handle(QJsonDocument::fromJson(text.toUtf8()).object());
}

void Gateway::handle(const QJsonObject &msg) {
    const int op = i32(msg, "op", -1);
    if (!isNull(msg, "s")) m_seq = i64(msg, "s", m_seq);

    switch (op) {
        case Hello: {
            const int interval = i32(obj(msg, "d"), "heartbeat_interval", 41250);
            m_acked = true;
            m_heartbeat.start(interval);
            // first heartbeat after interval * jitter, as the spec asks (bound to this socket)
            QTimer::singleShot(static_cast<int>(interval * QRandomGenerator::global()->generateDouble()), m_ws, [this]() {
                sendHeartbeat();
            });
            if (m_canResume) resume();
            else identify();
            break;
        }
        case HeartbeatAck:
            m_acked = true;
            break;
        case Heartbeat: // the server asks for one right now; always answer
            send(Heartbeat, m_seq < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(m_seq));
            break;
        case Reconnect:
            if (m_ws) m_ws->close(QWebSocketProtocol::CloseCode(4000));
            break;
        case InvalidSession: {
            const bool resumable = msg.value("d").toBool(false);
            qInfo() << "Invalid session, resumable:" << resumable;
            if (!resumable) {
                m_seq = -1;
                m_sessionId.clear();
                m_canResume = false;
            }
            QTimer::singleShot(1000 + QRandomGenerator::global()->bounded(4000), this, [this, resumable]() {
                if (resumable) resume();
                else identify();
            });
            break;
        }
        case Dispatch: {
            const auto t = str(msg, "t");
            QJsonObject d = obj(msg, "d");
            if (msg.value("d").isArray()) d.insert("_array", msg.value("d")); // e.g. SESSIONS_REPLACE
            if (t == "READY") {
                m_sessionId = str(d, "session_id");
                m_resumeUrl = str(d, "resume_gateway_url");
                m_ready = true;
                m_canResume = true;
                m_resumeAttempts = 0;
                m_backoff = 1000;
                emit stateChanged("connected");
            } else if (t == "RESUMED") {
                m_ready = true;
                m_resumeAttempts = 0;
                m_backoff = 1000;
                emit stateChanged("connected");
            }
            emit dispatch(t, d);
            break;
        }
        default:
            break;
    }
}

void Gateway::send(int op, const QJsonValue &d) {
    if (!m_ws || m_ws->state() != QAbstractSocket::ConnectedState) return;
    QJsonObject msg { { "op", op }, { "d", d } };
    m_ws->sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void Gateway::sendHeartbeat() {
    if (!m_acked) {
        qWarning() << "Heartbeat not acknowledged; reconnecting";
        if (m_ws) m_ws->close(QWebSocketProtocol::CloseCode(4000));
        return;
    }
    m_acked = false;
    send(Heartbeat, m_seq < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(m_seq));
}

void Gateway::identify() {
    QJsonObject presence {
        { "status", "unknown" },
        { "since", 0 },
        { "activities", QJsonArray {} },
        { "afk", false },
    };
    QJsonObject d {
        { "token", m_token },
        { "capabilities", 4605 },
        { "properties", m_http->identifyProperties() },
        { "presence", presence },
        { "compress", false },
        { "client_state", QJsonObject { { "guild_versions", QJsonObject {} } } },
    };
    send(Identify, d);
}

void Gateway::resume() {
    send(Resume, QJsonObject {
                     { "token", m_token },
                     { "session_id", m_sessionId },
                     { "seq", m_seq },
                 });
}

} // namespace kestrel
