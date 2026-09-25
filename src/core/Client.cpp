#include "Client.h"

#include "Json.h"

#include <QDateTime>
#include <QJsonArray>
#include <QUrl>

namespace kestrel {

using namespace json;

Client::Client(const Endpoints &ep, QObject *parent)
    : QObject(parent)
    , m_ep(ep)
    , m_http(new Http(ep, this))
    , m_store(new Store(this))
    , m_gateway(new Gateway(ep.gateway, m_http, this)) {
    connect(m_gateway, &Gateway::dispatch, m_store, &Store::handleDispatch);
    connect(m_gateway, &Gateway::stateChanged, this, &Client::connectionStateChanged);
    connect(m_gateway, &Gateway::authenticationFailed, this, [this]() {
        emit loggedOut("Your session is no longer valid. Please log in again.");
    });
    connect(m_store, &Store::authTokenRotated, this, [this](const QString &t) {
        m_token = t;
        m_http->setToken(t);
        m_gateway->setToken(t);
        emit tokenChanged(t);
    });

    m_memberTimer.setSingleShot(true);
    m_memberTimer.setInterval(250);
    connect(&m_memberTimer, &QTimer::timeout, this, &Client::flushMemberRequests);
    m_typingThrottle.setSingleShot(true);
    m_typingThrottle.setInterval(8000);
}

void Client::start(const QString &token) {
    m_token = token;
    m_http->setToken(token);
    m_http->bootstrap([this]() { m_gateway->connectWithToken(m_token); });
}

void Client::stop() {
    m_gateway->disconnectFromHost();
    m_store->clear();
    m_requestedMembers.clear();
    m_pendingMembers.clear();
}

static void parseList(Store *store, const HttpResponse &r, const Client::MessagesCallback &cb) {
    QVector<Message> out;
    if (r.ok()) {
        const auto list = r.json.array();
        out.reserve(list.size());
        for (const auto &v : list) out.append(store->parseMessage(v.toObject()));
    }
    cb(r.ok(), out);
}

void Client::fetchMessages(Snowflake channelId, Snowflake before, int limit, MessagesCallback cb) {
    QString path = QString("/channels/%1/messages?limit=%2").arg(channelId).arg(limit);
    if (before) path += QString("&before=%1").arg(before);
    m_http->get(path, [this, cb](const HttpResponse &r) { parseList(m_store, r, cb); });
}

void Client::fetchMessagesAround(Snowflake channelId, Snowflake around, MessagesCallback cb) {
    m_http->get(QString("/channels/%1/messages?limit=50&around=%2").arg(channelId).arg(around),
                [this, cb](const HttpResponse &r) { parseList(m_store, r, cb); });
}

void Client::sendMessage(Snowflake channelId, const QString &content, Snowflake replyTo, bool mentionReply,
                         const QStringList &files, const QString &nonce, std::function<void(bool, const QString &)> cb) {
    QJsonObject body {
        { "content", content },
        { "nonce", nonce },
        { "tts", false },
        { "flags", 0 },
        { "mobile_network_type", "unknown" },
    };
    if (replyTo) {
        body["message_reference"] = QJsonObject { { "channel_id", idStr(channelId) }, { "message_id", idStr(replyTo) } };
        if (auto *c = m_store->channel(channelId); c && c->guildId)
            body["message_reference"] = QJsonObject { { "guild_id", idStr(c->guildId) }, { "channel_id", idStr(channelId) }, { "message_id", idStr(replyTo) } };
        if (!mentionReply) body["allowed_mentions"] = QJsonObject { { "parse", QJsonArray { "users", "roles", "everyone" } }, { "replied_user", false } };
    }
    auto done = [cb](const HttpResponse &r) {
        QString err;
        if (!r.ok()) {
            err = str(r.json.object(), "message");
            if (err.isEmpty()) err = r.error.isEmpty() ? QString("Error %1").arg(r.status) : r.error;
        }
        if (cb) cb(r.ok(), err);
    };
    const QString path = QString("/channels/%1/messages").arg(channelId);
    if (files.isEmpty()) m_http->post(path, body, done);
    else m_http->postMultipart(path, body, files, done);
}

void Client::editMessage(Snowflake channelId, Snowflake messageId, const QString &content) {
    m_http->patch(QString("/channels/%1/messages/%2").arg(channelId).arg(messageId), { { "content", content } });
}

void Client::deleteMessage(Snowflake channelId, Snowflake messageId) {
    m_http->del(QString("/channels/%1/messages/%2").arg(channelId).arg(messageId));
}

static QString emojiPath(const QString &emoji) {
    return QString::fromLatin1(QUrl::toPercentEncoding(emoji));
}

void Client::addReaction(Snowflake channelId, Snowflake messageId, const QString &emoji) {
    m_http->put(QString("/channels/%1/messages/%2/reactions/%3/@me?location=Message&type=0").arg(channelId).arg(messageId).arg(emojiPath(emoji)));
}

void Client::removeReaction(Snowflake channelId, Snowflake messageId, const QString &emoji) {
    m_http->del(QString("/channels/%1/messages/%2/reactions/%3/0/@me?location=Message&burst=false").arg(channelId).arg(messageId).arg(emojiPath(emoji)));
}

void Client::sendTyping(Snowflake channelId) {
    if (m_typingThrottle.isActive() && m_lastTypingChannel == channelId) return;
    m_lastTypingChannel = channelId;
    m_typingThrottle.start();
    m_http->postEmpty(QString("/channels/%1/typing").arg(channelId));
}

void Client::ack(Snowflake channelId, Snowflake messageId) {
    if (!messageId) return;
    const auto &rs = m_store->readStates.value(channelId);
    const bool needed = rs.lastMessageId < messageId || rs.mentionCount > 0;
    m_store->markRead(channelId, messageId);
    if (!needed) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastAck.value(channelId) < 1500) return; // don't spam acks while scrolling
    m_lastAck[channelId] = now;
    m_http->post(QString("/channels/%1/messages/%2/ack").arg(channelId).arg(messageId), { { "token", QJsonValue::Null } });
}

void Client::setStatus(const QString &status) {
    m_gateway->send(Gateway::PresenceUpdate, QJsonObject {
                                                 { "status", status },
                                                 { "since", 0 },
                                                 { "activities", QJsonArray {} },
                                                 { "afk", false },
                                             });
    m_store->selfStatus = status;
    emit m_store->selfChanged();
}

void Client::requestMembers(Snowflake guildId, const QVector<Snowflake> &userIds) {
    if (!guildId) return;
    for (auto u : userIds) {
        if (m_store->member(guildId, u) || m_requestedMembers.contains({ guildId, u })) continue;
        m_requestedMembers.insert({ guildId, u });
        m_pendingMembers[guildId].insert(u);
    }
    if (!m_pendingMembers.isEmpty() && !m_memberTimer.isActive()) m_memberTimer.start();
}

void Client::flushMemberRequests() {
    for (auto it = m_pendingMembers.begin(); it != m_pendingMembers.end(); ++it) {
        QJsonArray ids;
        for (auto u : it.value()) {
            ids.append(idStr(u));
            if (ids.size() == 100) {
                m_gateway->send(Gateway::RequestGuildMembers, QJsonObject { { "guild_id", idStr(it.key()) }, { "user_ids", ids }, { "presences", false } });
                ids = {};
            }
        }
        if (!ids.isEmpty())
            m_gateway->send(Gateway::RequestGuildMembers, QJsonObject { { "guild_id", idStr(it.key()) }, { "user_ids", ids }, { "presences", false } });
    }
    m_pendingMembers.clear();
}

void Client::subscribeGuild(Snowflake guildId, Snowflake channelId, int rangeEnd) {
    if (!guildId) return;
    QJsonArray ranges;
    for (int start = 0; start <= rangeEnd; start += 100) ranges.append(QJsonArray { start, start + 99 });
    QJsonObject sub {
        { "typing", true },
        { "threads", true },
        { "activities", true },
        { "member_updates", false },
    };
    if (channelId) sub["channels"] = QJsonObject { { idStr(channelId), ranges } };
    m_gateway->send(Gateway::GuildSubscriptionsBulk, QJsonObject { { "subscriptions", QJsonObject { { idStr(guildId), sub } } } });
}

void Client::updateVoiceState(Snowflake guildId, Snowflake channelId, bool selfMute, bool selfDeaf) {
    m_gateway->send(Gateway::VoiceStateUpdate, QJsonObject {
                                                   { "guild_id", guildId ? QJsonValue(idStr(guildId)) : QJsonValue(QJsonValue::Null) },
                                                   { "channel_id", channelId ? QJsonValue(idStr(channelId)) : QJsonValue(QJsonValue::Null) },
                                                   { "self_mute", selfMute },
                                                   { "self_deaf", selfDeaf },
                                                   { "self_video", false },
                                                   { "flags", 2 },
                                               });
}

} // namespace kestrel
