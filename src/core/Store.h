#pragma once

#include "Protobuf.h"
#include "Types.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>

namespace kestrel {

// Everything the client knows about the account, kept up to date from READY
// and later gateway events. Parsing is defensive: unknown or malformed fields
// are skipped rather than aborting the whole payload.
class Store : public QObject {
    Q_OBJECT

public:
    explicit Store(QObject *parent = nullptr);

    void clear();
    void handleDispatch(const QString &event, const QJsonObject &d);

    // parsing helpers (also used by REST responses)
    User &upsertUser(const QJsonObject &o);
    Message parseMessage(const QJsonObject &o);
    Channel parseChannel(const QJsonObject &o, Snowflake guildId);
    void applyMessageUpdate(Message &m, const QJsonObject &o);
    void storeMember(Snowflake guildId, const QJsonObject &member, Snowflake userId = 0);

    // queries
    const User *user(Snowflake id) const;
    const Guild *guild(Snowflake id) const;
    const Channel *channel(Snowflake id) const;
    QString userDisplayName(Snowflake userId, Snowflake guildId = 0) const;
    quint32 userColor(Snowflake userId, Snowflake guildId) const;
    const MemberInfo *member(Snowflake guildId, Snowflake userId) const;
    QVector<Snowflake> memberRoles(Snowflake guildId, Snowflake userId) const;
    quint64 permissions(Snowflake channelId) const;
    bool canView(Snowflake channelId) const;

    bool isUnread(Snowflake channelId) const;
    int mentionCount(Snowflake channelId) const;
    bool isChannelMuted(Snowflake channelId) const;
    bool isGuildMuted(Snowflake guildId) const;
    bool guildUnread(Snowflake guildId) const;
    int guildMentions(Snowflake guildId) const;
    void markRead(Snowflake channelId, Snowflake messageId);

    QString presence(Snowflake userId) const { return presences.value(userId, "offline"); }
    QVector<VoiceState> voiceStatesIn(Snowflake channelId) const;
    const VoiceState *voiceState(Snowflake userId) const;
    QVector<Snowflake> typingIn(Snowflake channelId);

    QString privateChannelName(const Channel &c) const;
    QVector<Snowflake> sortedPrivateChannels() const;

    // data (read directly by models; mutate only through handleDispatch)
    User self;
    QString selfStatus = "online";
    QHash<Snowflake, User> users;
    QHash<Snowflake, Guild> guilds;
    QVector<Snowflake> guildOrder;
    QVector<GuildFolder> folders;
    QHash<Snowflake, Channel> channels;
    QVector<Snowflake> privateChannels;
    QHash<Snowflake, ReadState> readStates;
    QSet<Snowflake> mutedGuilds;
    QSet<Snowflake> mutedChannels;
    QHash<Snowflake, VoiceState> voiceStates; // by user id
    QHash<Snowflake, QString> presences;
    QHash<Snowflake, QHash<Snowflake, qint64>> typing; // channel -> user -> expiry ms
    QHash<QPair<Snowflake, Snowflake>, MemberInfo> members; // (guild, user)
    QSet<Snowflake> relationshipsFriends;
    QString authToken; // rotated token from READY, if the server issued one
    bool ready = false;

signals:
    void readyReceived();
    void guildsChanged();
    void guildChanged(Snowflake guildId);
    void channelsChanged(Snowflake guildId);
    void privateChannelsChanged();
    void unreadChanged(Snowflake channelId);
    void messageCreated(const kestrel::Message &m);
    void messageUpdated(Snowflake channelId, Snowflake messageId, const QJsonObject &data);
    void messageDeleted(Snowflake channelId, Snowflake messageId);
    void reactionChanged(Snowflake channelId, Snowflake messageId, const QJsonObject &emoji, int delta, bool me);
    void reactionsCleared(Snowflake channelId, Snowflake messageId);
    void typingChanged(Snowflake channelId);
    void voiceStateChanged(const kestrel::VoiceState &state, Snowflake previousChannel);
    void voiceServerUpdate(Snowflake guildId, const QString &endpoint, const QString &token);
    void presenceChanged(Snowflake userId);
    void memberListUpdate(Snowflake guildId, const QJsonObject &data);
    void membersChanged(Snowflake guildId);
    void selfChanged();
    void authTokenRotated(const QString &token);

private:
    void handleReady(const QJsonObject &d);
    void handleReadySupplemental(const QJsonObject &d);
    Guild parseGuild(const QJsonObject &o);
    void ingestGuild(const QJsonObject &o);
    void setVoiceState(const QJsonObject &o, Snowflake guildId);
    void setPresence(const QJsonObject &o);
    void rebuildGuildOrder();
};

} // namespace kestrel
