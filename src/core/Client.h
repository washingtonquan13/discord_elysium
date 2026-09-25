#pragma once

#include "Gateway.h"
#include "Http.h"
#include "Store.h"

#include <QObject>
#include <QSet>
#include <QTimer>
#include <functional>

namespace kestrel {

// Account session: owns the REST client, gateway and data store, and offers
// the operations the UI needs. Everything runs on the main thread.
class Client : public QObject {
    Q_OBJECT

public:
    explicit Client(const Endpoints &ep, QObject *parent = nullptr);

    Http *http() { return m_http; }
    Store *store() { return m_store; }
    Gateway *gateway() { return m_gateway; }
    const Endpoints &endpoints() const { return m_ep; }

    void start(const QString &token);
    void stop();
    QString token() const { return m_token; }

    using MessagesCallback = std::function<void(bool ok, const QVector<Message> &messages)>;
    void fetchMessages(Snowflake channelId, Snowflake before, int limit, MessagesCallback cb);
    void fetchMessagesAround(Snowflake channelId, Snowflake around, MessagesCallback cb);
    void sendMessage(Snowflake channelId, const QString &content, Snowflake replyTo, bool mentionReply,
                     const QStringList &files, const QString &nonce, std::function<void(bool, const QString &)> cb);
    void editMessage(Snowflake channelId, Snowflake messageId, const QString &content);
    void deleteMessage(Snowflake channelId, Snowflake messageId);
    void addReaction(Snowflake channelId, Snowflake messageId, const QString &emoji);
    void removeReaction(Snowflake channelId, Snowflake messageId, const QString &emoji);
    void sendTyping(Snowflake channelId);
    void ack(Snowflake channelId, Snowflake messageId);
    void setStatus(const QString &status);

    void requestMembers(Snowflake guildId, const QVector<Snowflake> &userIds);
    void resetMemberRequests() { m_requestedMembers.clear(); m_pendingMembers.clear(); }
    void subscribeGuild(Snowflake guildId, Snowflake channelId, int rangeEnd = 99);

    // voice signalling on the main gateway
    void updateVoiceState(Snowflake guildId, Snowflake channelId, bool selfMute, bool selfDeaf);

signals:
    void connectionStateChanged(const QString &state);
    void loggedOut(const QString &reason);
    void tokenChanged(const QString &token);

private:
    void flushMemberRequests();

    Endpoints m_ep;
    Http *m_http;
    Store *m_store;
    Gateway *m_gateway;
    QString m_token;
    QHash<Snowflake, QSet<Snowflake>> m_pendingMembers;
    QSet<QPair<Snowflake, Snowflake>> m_requestedMembers;
    QTimer m_memberTimer;
    QTimer m_typingThrottle;
    Snowflake m_lastTypingChannel = 0;
    QHash<Snowflake, qint64> m_lastAck;
};

} // namespace kestrel
