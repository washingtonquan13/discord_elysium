#pragma once

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QWebSocket>
#include <zlib.h>

namespace kestrel {

class Http;

// The main Discord gateway connection: zlib-stream decompression, heartbeats,
// identify/resume and automatic reconnection with backoff.
class Gateway : public QObject {
    Q_OBJECT

public:
    enum Op {
        Dispatch = 0,
        Heartbeat = 1,
        Identify = 2,
        PresenceUpdate = 3,
        VoiceStateUpdate = 4,
        Resume = 6,
        Reconnect = 7,
        RequestGuildMembers = 8,
        InvalidSession = 9,
        Hello = 10,
        HeartbeatAck = 11,
        GuildSubscriptionsBulk = 37,
    };

    Gateway(const QString &url, Http *http, QObject *parent = nullptr);
    ~Gateway() override;

    void connectWithToken(const QString &token);
    void disconnectFromHost();
    bool isConnected() const { return m_ready; }
    void send(int op, const QJsonValue &d);
    QString sessionId() const { return m_sessionId; }
    void setToken(const QString &token) { m_token = token; }

signals:
    void dispatch(const QString &event, const QJsonObject &data);
    void stateChanged(const QString &state);
    void authenticationFailed();

private:
    void open();
    void onBinary(const QByteArray &data);
    void onText(const QString &text);
    void handle(const QJsonObject &msg);
    void identify();
    void resume();
    void sendHeartbeat();
    void scheduleReconnect(bool canResume);
    void resetInflate();

    QString m_url;
    QString m_resumeUrl;
    Http *m_http;
    QWebSocket *m_ws = nullptr;
    QTimer m_heartbeat;
    QTimer m_reconnect;
    z_stream m_zs {};
    bool m_zsInit = false;
    QByteArray m_zbuf;
    QString m_token;
    QString m_sessionId;
    qint64 m_seq = -1;
    bool m_acked = true;
    bool m_ready = false;
    bool m_wantConnected = false;
    bool m_canResume = false;
    int m_backoff = 1000;
    int m_resumeAttempts = 0;
};

} // namespace kestrel
