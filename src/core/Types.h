#pragma once
// Compact in-memory representations of Discord objects. We deliberately keep
// only the fields the UI uses instead of holding raw JSON, which is the main
// reason Kestrel's resident memory stays small on accounts with many servers.

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>
#include <cstdint>

namespace kestrel {

using Snowflake = quint64;

inline QDateTime snowflakeTime(Snowflake id) {
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>((id >> 22) + 1420070400000ULL));
}

inline Snowflake makeNonce() {
    static quint64 counter = 0;
    const quint64 ms = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) - 1420070400000ULL;
    return (ms << 22) | (++counter & 0x3FFFFF);
}

enum class ChannelType : int {
    GuildText = 0,
    DM = 1,
    GuildVoice = 2,
    GroupDM = 3,
    GuildCategory = 4,
    GuildNews = 5,
    GuildStore = 6,
    NewsThread = 10,
    PublicThread = 11,
    PrivateThread = 12,
    GuildStageVoice = 13,
    GuildDirectory = 14,
    GuildForum = 15,
    GuildMedia = 16,
};

struct User {
    Snowflake id = 0;
    QString username;
    QString globalName;
    QString discriminator;
    QString avatar;
    bool bot = false;

    QString displayName() const {
        return globalName.isEmpty() ? username : globalName;
    }
};

struct Role {
    Snowflake id = 0;
    QString name;
    quint32 color = 0;
    int position = 0;
    quint64 permissions = 0;
    bool hoist = false;
};

struct Overwrite {
    Snowflake id = 0;
    int type = 0; // 0 role, 1 member
    quint64 allow = 0;
    quint64 deny = 0;
};

struct Channel {
    Snowflake id = 0;
    Snowflake guildId = 0;
    Snowflake parentId = 0;
    Snowflake lastMessageId = 0;
    Snowflake ownerId = 0;
    ChannelType type = ChannelType::GuildText;
    QString name;
    QString topic;
    QString icon;
    int position = 0;
    int userLimit = 0;
    bool nsfw = false;
    QVector<Overwrite> overwrites;
    QVector<Snowflake> recipients;

    bool isText() const {
        return type == ChannelType::GuildText || type == ChannelType::GuildNews || type == ChannelType::DM || type == ChannelType::GroupDM
            || type == ChannelType::PublicThread || type == ChannelType::PrivateThread || type == ChannelType::NewsThread;
    }
    bool isVoice() const {
        return type == ChannelType::GuildVoice || type == ChannelType::GuildStageVoice;
    }
    bool isPrivate() const {
        return type == ChannelType::DM || type == ChannelType::GroupDM;
    }
};

struct CustomEmoji {
    Snowflake id = 0;
    QString name;
    bool animated = false;
};

struct MemberInfo {
    QString nick;
    QString avatar;
    QVector<Snowflake> roles;
};

struct Guild {
    Snowflake id = 0;
    QString name;
    QString icon;
    Snowflake ownerId = 0;
    int memberCount = 0;
    bool unavailable = false;
    bool large = false;
    QHash<Snowflake, Role> roles;
    QVector<Snowflake> channels;
    QVector<CustomEmoji> emojis;
    MemberInfo self;
};

struct ReadState {
    Snowflake lastMessageId = 0;
    int mentionCount = 0;
};

struct VoiceState {
    Snowflake userId = 0;
    Snowflake guildId = 0;
    Snowflake channelId = 0;
    QString sessionId;
    bool selfMute = false;
    bool selfDeaf = false;
    bool mute = false;
    bool deaf = false;
    bool selfStream = false;
    bool selfVideo = false;
};

struct Attachment {
    Snowflake id = 0;
    QString filename;
    QString url;
    QString proxyUrl;
    QString contentType;
    qint64 size = 0;
    int width = 0;
    int height = 0;
    bool spoiler = false;
};

struct EmbedField {
    QString name;
    QString value;
    bool inlineField = false;
};

struct EmbedMedia {
    QString url;
    QString proxyUrl;
    int width = 0;
    int height = 0;
};

struct Embed {
    QString type;
    QString title;
    QString description;
    QString url;
    int color = -1;
    QString authorName;
    QString authorIcon;
    QString providerName;
    QString footer;
    EmbedMedia image;
    EmbedMedia thumbnail;
    QVector<EmbedField> fields;
};

struct Reaction {
    Snowflake emojiId = 0;
    QString emojiName;
    bool animated = false;
    int count = 0;
    bool me = false;
};

struct Message {
    Snowflake id = 0;
    Snowflake channelId = 0;
    Snowflake guildId = 0;
    Snowflake authorId = 0;
    Snowflake webhookId = 0;
    int type = 0;
    int flags = 0;
    QString content;
    QDateTime timestamp;
    QDateTime edited;
    QVector<Attachment> attachments;
    QVector<Embed> embeds;
    QVector<Reaction> reactions;
    QVector<Snowflake> mentions;
    QVector<Snowflake> mentionRoles;
    QStringList stickers;
    bool mentionEveryone = false;
    bool pinned = false;
    // reply context
    Snowflake replyToId = 0;
    Snowflake replyAuthorId = 0;
    QString replyContent;
    bool replyDeleted = false;
    // local send state
    QString nonce;
    bool pending = false;
    bool failed = false;
};

} // namespace kestrel
