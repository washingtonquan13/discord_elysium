#pragma once

#include "core/Types.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QWebSocket>
#include <memory>

namespace kestrel {

class AudioEngine;
class DaveSession;
class Settings;
class VoiceUdp;

// One voice connection: voice gateway signalling, UDP media transport,
// transport encryption and DAVE end-to-end encryption.
class VoiceClient : public QObject {
    Q_OBJECT

public:
    VoiceClient(AudioEngine *audio, Settings *settings, QObject *parent = nullptr);
    ~VoiceClient() override;

    // Both halves arrive from the main gateway in either order.
    void setSession(Snowflake userId, const QString &sessionId);
    void setServer(Snowflake guildId, Snowflake channelId, const QString &endpoint, const QString &token);
    void disconnectVoice();

    QString state() const { return m_state; }
    bool encrypted() const { return m_daveActive; }
    QSet<Snowflake> speakingUsers(); // polled by the UI
    void setUserVolume(Snowflake userId, float volume);
    int ping() const { return m_ping; }

signals:
    void stateChanged(const QString &state);
    void encryptionChanged(bool e2ee);

private:
    void tryConnect();
    void onText(const QString &text);
    void onBinary(const QByteArray &data);
    void sendJson(int op, const QJsonValue &d);
    void sendBinary(int op, const std::vector<uint8_t> &payload);
    void setState(const QString &s);
    void ensureDave(int version);
    void mapUser(uint32_t ssrc, Snowflake userId);
    void teardownPipe();

    AudioEngine *m_audio;
    Settings *m_settings;
    std::unique_ptr<VoiceUdp> m_udp;
    std::unique_ptr<DaveSession> m_dave;
    QWebSocket *m_ws = nullptr;
    QTimer m_heartbeat;
    QTimer m_keepalive;
    QString m_state = "disconnected";
    Snowflake m_userId = 0;
    Snowflake m_guildId = 0;
    Snowflake m_channelId = 0;
    QString m_sessionId;
    QString m_endpoint;
    QString m_token;
    uint32_t m_ssrc = 0;
    int m_seq = -1;
    qint64 m_lastHeartbeat = 0;
    int m_ping = 0;
    bool m_daveActive = false;
    int m_reconnects = 0;
    QHash<uint32_t, Snowflake> m_ssrcUsers;
    QSet<QString> m_connectedUsers;
};

} // namespace kestrel
