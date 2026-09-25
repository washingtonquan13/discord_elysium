#include "MessageModel.h"

#include "Urls.h"
#include "core/Json.h"
#include "core/Settings.h"

#include <QColor>
#include <QMap>
#include <QSet>
#include <QLocale>
#include <QRegularExpression>
#include <QUrl>

namespace kestrel {

using namespace json;

static constexpr int kPageSize = 50;
static constexpr int kCachedChannels = 6;
static constexpr int kMaxCachedMessages = 200;

MessageModel::MessageModel(Client *client, Settings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_client(client)
    , m_store(client->store())
    , m_settings(settings) {
    connect(m_store, &Store::messageCreated, this, &MessageModel::onCreated);
    connect(m_store, &Store::messageUpdated, this, &MessageModel::onUpdated);
    connect(m_store, &Store::messageDeleted, this, &MessageModel::onDeleted);
    connect(m_store, &Store::reactionChanged, this, &MessageModel::onReaction);
    connect(m_store, &Store::reactionsCleared, this, &MessageModel::onReactionsCleared);
    connect(m_store, &Store::membersChanged, this, [this](Snowflake g) {
        if (g == m_guild) refreshAuthors();
    });
}

QHash<int, QByteArray> MessageModel::roleNames() const {
    return {
        { IdRole, "messageId" }, { AuthorIdRole, "authorId" }, { AuthorNameRole, "authorName" }, { AuthorColorRole, "authorColor" },
        { AvatarRole, "avatar" }, { TimeRole, "time" }, { FullTimeRole, "fullTime" }, { ShortTimeRole, "shortTime" },
        { HtmlRole, "html" }, { PlainRole, "plain" }, { GroupedRole, "grouped" }, { EditedRole, "edited" },
        { AttachmentsRole, "attachments" }, { EmbedsRole, "embeds" }, { ReactionsRole, "reactions" }, { ReplyRole, "reply" },
        { MentionsMeRole, "mentionsMe" }, { SystemRole, "systemText" }, { PendingRole, "pending" }, { FailedRole, "failed" },
        { DayDividerRole, "dayDivider" }, { FirstUnreadRole, "firstUnread" }, { IsBotRole, "isBot" }, { IsSelfRole, "isSelf" },
        { StickersRole, "stickers" }, { JumboRole, "jumbo" },
    };
}

void MessageModel::cacheCurrent() {
    if (!m_channel) return;
    Cached c;
    c.messages = m_messages.mid(0, kMaxCachedMessages);
    c.hasMore = m_hasMore || m_messages.size() > kMaxCachedMessages;
    m_cache.insert(m_channel, c);
    m_lru.removeAll(m_channel);
    m_lru.prepend(m_channel);
    while (m_lru.size() > kCachedChannels) m_cache.remove(m_lru.takeLast());
}

void MessageModel::setChannel(Snowflake channelId) {
    if (channelId == m_channel) return;
    cacheCurrent();

    beginResetModel();
    m_channel = channelId;
    const Channel *c = m_store->channel(channelId);
    m_guild = c ? c->guildId : 0;
    m_readMarker = m_store->readStates.value(channelId).lastMessageId;
    m_html.clear();
    m_generation++;
    if (auto it = m_cache.find(channelId); it != m_cache.end()) {
        m_messages = it->messages;
        m_hasMore = it->hasMore;
    } else {
        m_messages.clear();
        m_hasMore = true;
    }
    endResetModel();
    if (!channelId) return;

    // Always refresh the newest page; merge into what we already show.
    m_loading = true;
    emit loadingChanged();
    const int gen = m_generation;
    m_client->fetchMessages(channelId, 0, kPageSize, [this, gen, channelId](bool ok, const QVector<Message> &msgs) {
        if (gen != m_generation) return;
        m_loading = false;
        emit loadingChanged();
        if (!ok) return;
        // Does the fetched page connect to what we already show (cache or live events)?
        QSet<Snowflake> have;
        for (const auto &m : m_messages)
            if (m.id) have.insert(m.id);
        bool overlaps = false;
        for (const auto &m : msgs)
            if (have.contains(m.id)) { overlaps = true; break; }
        if (overlaps || (m_messages.size() && !msgs.isEmpty() && msgs.last().id <= m_messages.last().id)) {
            // merge by id, newest first, keeping local pending/failed rows at the top
            QVector<Message> merged;
            QVector<Message> local;
            for (const auto &m : m_messages)
                if (m.pending || m.failed) local.append(m);
            QMap<Snowflake, Message> byId;
            for (const auto &m : m_messages)
                if (!m.pending && !m.failed) byId.insert(m.id, m);
            for (const auto &m : msgs) byId.insert(m.id, m);
            for (auto it = byId.end(); it != byId.begin();) {
                --it;
                merged.append(it.value());
            }
            beginResetModel();
            m_messages = local + merged;
            m_html.clear();
            endResetModel();
        } else {
            beginResetModel();
            m_messages = msgs;
            m_hasMore = msgs.size() >= kPageSize;
            m_html.clear();
            endResetModel();
        }
        requestAuthors(msgs);
        Q_UNUSED(channelId);
    });
}

void MessageModel::invalidate() {
    m_cache.clear();
    m_lru.clear();
    // the open channel may have missed messages while we were disconnected: reload it
    const Snowflake current = m_channel;
    if (current) {
        beginResetModel();
        m_channel = 0;
        m_messages.clear();
        m_html.clear();
        endResetModel();
        setChannel(current);
    }
}

void MessageModel::loadMore() {
    if (m_loading || !m_hasMore || !m_channel || m_messages.isEmpty()) return;
    m_loading = true;
    emit loadingChanged();
    const int gen = m_generation;
    m_client->fetchMessages(m_channel, m_messages.last().id, kPageSize, [this, gen](bool ok, const QVector<Message> &msgs) {
        if (gen != m_generation) return;
        m_loading = false;
        if (ok) {
            m_hasMore = msgs.size() >= kPageSize;
            if (!msgs.isEmpty()) {
                // grouping of the previously-oldest row may change
                const int oldLast = m_messages.size() - 1;
                beginInsertRows({}, m_messages.size(), m_messages.size() + msgs.size() - 1);
                m_messages += msgs;
                endInsertRows();
                if (oldLast >= 0) emit dataChanged(index(oldLast), index(oldLast), { GroupedRole, DayDividerRole, FirstUnreadRole });
            }
            requestAuthors(msgs);
        }
        emit loadingChanged();
    });
}

void MessageModel::jumpTo(const QString &messageId) {
    const Snowflake id = messageId.toULongLong();
    if (int row = rowOf(id); row >= 0) {
        emit positionRequested(row);
        return;
    }
    m_loading = true;
    emit loadingChanged();
    const int gen = ++m_generation;
    m_client->fetchMessagesAround(m_channel, id, [this, gen, id](bool ok, const QVector<Message> &msgs) {
        if (gen != m_generation) return;
        m_loading = false;
        emit loadingChanged();
        if (!ok) return;
        beginResetModel();
        m_messages = msgs;
        m_hasMore = true;
        m_html.clear();
        endResetModel();
        requestAuthors(msgs);
        if (int row = rowOf(id); row >= 0) emit positionRequested(row);
    });
}

QString MessageModel::rawContent(const QString &messageId) const {
    const int row = rowOf(messageId.toULongLong());
    return row >= 0 ? m_messages[row].content : QString();
}

QString MessageModel::lastOwnMessageId() const {
    for (const auto &m : m_messages)
        if (m.authorId == m_store->self.id && !m.pending) return QString::number(m.id);
    return {};
}

void MessageModel::addPending(const Message &m) {
    if (m.channelId != m_channel) return;
    beginInsertRows({}, 0, 0);
    m_messages.prepend(m);
    endInsertRows();
    if (m_messages.size() > 1) emit dataChanged(index(1), index(1), { GroupedRole });
    emit newMessageArrived(true);
}

void MessageModel::markFailed(const QString &nonce) {
    for (int i = 0; i < m_messages.size(); i++) {
        if (m_messages[i].pending && m_messages[i].nonce == nonce) {
            m_messages[i].pending = false;
            m_messages[i].failed = true;
            emit dataChanged(index(i), index(i), { PendingRole, FailedRole });
            return;
        }
    }
}

void MessageModel::refreshAuthors() {
    if (m_messages.isEmpty()) return;
    m_html.clear(); // mentions may resolve to names now
    emit dataChanged(index(0), index(m_messages.size() - 1), { AuthorNameRole, AuthorColorRole, AvatarRole, HtmlRole, ReplyRole });
}

void MessageModel::requestAuthors(const QVector<Message> &msgs) {
    if (!m_guild) return;
    QVector<Snowflake> ids;
    for (const auto &m : msgs) {
        if (!m.webhookId && m.authorId && !m_store->member(m_guild, m.authorId)) ids.append(m.authorId);
        if (m.replyAuthorId && !m_store->member(m_guild, m.replyAuthorId)) ids.append(m.replyAuthorId);
    }
    m_client->requestMembers(m_guild, ids);
}

int MessageModel::rowOf(Snowflake id) const {
    for (int i = 0; i < m_messages.size(); i++)
        if (m_messages[i].id == id) return i;
    return -1;
}

void MessageModel::onCreated(const Message &m) {
    if (m.channelId != m_channel) {
        auto it = m_cache.find(m.channelId);
        if (it != m_cache.end()) {
            it->messages.prepend(m);
            if (it->messages.size() > kMaxCachedMessages) it->messages.removeLast();
        }
        return;
    }
    // replace our optimistic copy when the server echoes it back
    if (!m.nonce.isEmpty()) {
        for (int i = 0; i < m_messages.size(); i++) {
            if ((m_messages[i].pending || m_messages[i].failed) && m_messages[i].nonce == m.nonce) {
                m_messages[i] = m;
                m_html.remove(m.id);
                emit dataChanged(index(i), index(i));
                return;
            }
        }
    }
    if (rowOf(m.id) >= 0) return;
    beginInsertRows({}, 0, 0);
    m_messages.prepend(m);
    endInsertRows();
    if (m_messages.size() > 1) emit dataChanged(index(1), index(1), { GroupedRole, DayDividerRole });
    requestAuthors({ m });
    emit newMessageArrived(m.authorId == m_store->self.id);
}

void MessageModel::onUpdated(Snowflake channelId, Snowflake id, const QJsonObject &d) {
    auto apply = [&](QVector<Message> &list) -> int {
        for (int i = 0; i < list.size(); i++) {
            if (list[i].id == id) {
                m_store->applyMessageUpdate(list[i], d);
                return i;
            }
        }
        return -1;
    };
    if (channelId != m_channel) {
        if (auto it = m_cache.find(channelId); it != m_cache.end()) apply(it->messages);
        return;
    }
    const int row = apply(m_messages);
    if (row >= 0) {
        m_html.remove(id);
        emit dataChanged(index(row), index(row));
    }
}

void MessageModel::onDeleted(Snowflake channelId, Snowflake id) {
    if (channelId != m_channel) {
        if (auto it = m_cache.find(channelId); it != m_cache.end())
            it->messages.erase(std::remove_if(it->messages.begin(), it->messages.end(), [id](const Message &m) { return m.id == id; }),
                               it->messages.end());
        return;
    }
    const int row = rowOf(id);
    if (row < 0) return;
    beginRemoveRows({}, row, row);
    m_messages.remove(row);
    endRemoveRows();
    m_html.remove(id);
    if (row < m_messages.size()) emit dataChanged(index(row), index(row), { GroupedRole, DayDividerRole });
    if (row > 0) emit dataChanged(index(row - 1), index(row - 1), { GroupedRole, DayDividerRole });
}

void MessageModel::onReaction(Snowflake channelId, Snowflake messageId, const QJsonObject &emoji, int delta, bool me) {
    if (channelId != m_channel) return;
    const int row = rowOf(messageId);
    if (row < 0) return;
    auto &list = m_messages[row].reactions;
    const Snowflake eid = id(emoji);
    const QString name = str(emoji, "name");
    int idx = -1;
    for (int i = 0; i < list.size(); i++) {
        if ((eid && list[i].emojiId == eid) || (!eid && list[i].emojiName == name)) { idx = i; break; }
    }
    if (idx < 0 && delta > 0) {
        Reaction r;
        r.emojiId = eid;
        r.emojiName = name;
        r.animated = boolean(emoji, "animated");
        list.append(r);
        idx = list.size() - 1;
    }
    if (idx < 0) return;
    list[idx].count += delta;
    if (me) list[idx].me = delta > 0;
    if (list[idx].count <= 0) list.remove(idx);
    emit dataChanged(index(row), index(row), { ReactionsRole });
}

void MessageModel::onReactionsCleared(Snowflake channelId, Snowflake messageId) {
    if (channelId != m_channel) return;
    const int row = rowOf(messageId);
    if (row < 0) return;
    // we don't know which emoji was cleared for REMOVE_EMOJI; refetching is overkill, clear all
    m_messages[row].reactions.clear();
    emit dataChanged(index(row), index(row), { ReactionsRole });
}

MarkdownContext MessageModel::markdownContext(Snowflake guildId) const {
    MarkdownContext ctx;
    ctx.userName = [this, guildId](Snowflake id) { return m_store->userDisplayName(id, guildId); };
    ctx.channelName = [this](Snowflake id) {
        const Channel *c = m_store->channel(id);
        return c ? c->name : QString();
    };
    ctx.roleName = [this, guildId](Snowflake id) -> QPair<QString, quint32> {
        const Guild *g = m_store->guild(guildId);
        if (!g) return { {}, 0 };
        auto it = g->roles.find(id);
        if (it == g->roles.end()) return { {}, 0 };
        return { it->name, it->color };
    };
    ctx.mapUrl = [](const QString &u) { return urls::endpoints().mapCdn(u); };
    return ctx;
}

QString MessageModel::html(const Message &m) const {
    if (auto it = m_html.find(m.id); it != m_html.end() && m.id) return *it;
    auto ctx = markdownContext(m.guildId ? m.guildId : m_guild);
    ctx.jumbo = isEmojiOnly(m.content);
    const QString out = markdownToHtml(m.content, ctx);
    if (m.id) m_html.insert(m.id, out);
    return out;
}

bool MessageModel::mentionsMe(const Message &m) const {
    const Snowflake me = m_store->self.id;
    if (m.mentions.contains(me) || m.mentionEveryone) return true;
    if (m.guildId) {
        const auto roles = m_store->memberRoles(m.guildId, me);
        for (auto r : m.mentionRoles)
            if (roles.contains(r)) return true;
    }
    return false;
}

QString MessageModel::systemText(const Message &m) const {
    const QString name = m_store->userDisplayName(m.authorId, m.guildId);
    switch (m.type) {
        case 1: return QString("%1 added %2 to the group.").arg(name, m.mentions.isEmpty() ? QString("someone") : m_store->userDisplayName(m.mentions.first()));
        case 2: return QString("%1 removed %2 from the group.").arg(name, m.mentions.isEmpty() ? QString("someone") : m_store->userDisplayName(m.mentions.first()));
        case 3: return QString("%1 started a call.").arg(name);
        case 4: return QString("%1 changed the channel name: %2").arg(name, m.content);
        case 5: return QString("%1 changed the channel icon.").arg(name);
        case 6: return QString("%1 pinned a message to this channel.").arg(name);
        case 7: {
            static const char *joins[] = { "%1 joined the party.", "%1 is here.", "Welcome, %1. We hope you brought pizza.",
                                           "A wild %1 appeared.", "%1 just landed.", "%1 just slid into the server.",
                                           "%1 just showed up!", "Welcome %1. Say hi!", "%1 hopped into the server.",
                                           "Everyone welcome %1!", "Glad you're here, %1.", "Good to see you, %1.",
                                           "Yay you made it, %1!" };
            const int idx = static_cast<int>((m.timestamp.toMSecsSinceEpoch()) % 13);
            return QString(joins[qAbs(idx)]).arg(name);
        }
        case 8: return QString("%1 just boosted the server!").arg(name);
        case 9: case 10: case 11: return QString("%1 just boosted the server! The server has reached a new level!").arg(name);
        case 12: return QString("%1 has added a channel to this channel's follows.").arg(name);
        case 18: return QString("%1 started a thread: %2").arg(name, m.content);
        case 24: return QString("AutoMod blocked a message.");
        case 46: return QString("A poll has closed.");
        default: return {};
    }
}

static QString formatTime(const QDateTime &t) {
    const QDateTime local = t.toLocalTime();
    const QDate today = QDate::currentDate();
    const QLocale loc;
    const QString time = loc.toString(local.time(), "h:mm AP");
    if (local.date() == today) return "Today at " + time;
    if (local.date() == today.addDays(-1)) return "Yesterday at " + time;
    return loc.toString(local.date(), "MM/dd/yyyy") + " " + time;
}

static QString humanSize(qint64 bytes) {
    if (bytes < 1024) return QString("%1 bytes").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

static QSize fitSize(int w, int h, int maxW, int maxH) {
    if (w <= 0 || h <= 0) return { maxW, maxH * 2 / 3 };
    QSize s(w, h);
    if (w > maxW || h > maxH) s = QSize(w, h).scaled(maxW, maxH, Qt::KeepAspectRatio);
    return s.expandedTo({ 16, 16 });
}

QVariantList MessageModel::attachments(const Message &m) const {
    QVariantList out;
    for (const auto &a : m.attachments) {
        QVariantMap v;
        const bool image = a.contentType.startsWith("image/") || QRegularExpression(R"(\.(png|jpe?g|gif|webp|bmp)$)", QRegularExpression::CaseInsensitiveOption).match(a.filename).hasMatch();
        const bool video = a.contentType.startsWith("video/");
        const QSize s = fitSize(a.width, a.height, 400, 300);
        v["filename"] = a.filename;
        v["size"] = humanSize(a.size);
        v["url"] = urls::endpoints().mapCdn(a.url);
        v["isImage"] = image;
        v["isVideo"] = video;
        v["isGif"] = a.contentType == "image/gif" || a.filename.endsWith(".gif", Qt::CaseInsensitive);
        v["spoiler"] = a.spoiler;
        v["width"] = s.width();
        v["height"] = s.height();
        v["fullWidth"] = a.width;
        v["fullHeight"] = a.height;
        if (image) {
            v["preview"] = urls::media(a.proxyUrl, a.url, s.width(), s.height());
            v["viewer"] = urls::media(a.proxyUrl, a.url, qMin(a.width > 0 ? a.width : 1600, 1600), qMin(a.height > 0 ? a.height : 1200, 1200));
        } else if (video && !a.proxyUrl.isEmpty()) {
            QString thumb = a.proxyUrl + (a.proxyUrl.contains('?') ? "&" : "?") + "format=jpeg";
            v["preview"] = urls::media(thumb, {}, s.width(), s.height());
        }
        out.append(v);
    }
    return out;
}

QVariantList MessageModel::embeds(const Message &m) const {
    QVariantList out;
    const auto ctx = markdownContext(m.guildId);
    for (const auto &e : m.embeds) {
        QVariantMap v;
        const bool mediaOnly = (e.type == "image" || e.type == "gifv") && e.title.isEmpty() && e.description.isEmpty();
        v["mediaOnly"] = mediaOnly;
        v["isGifv"] = e.type == "gifv";
        v["color"] = e.color >= 0 ? QColor::fromRgb(static_cast<QRgb>(e.color)).name() : QString("#1e1f22");
        v["provider"] = e.providerName;
        v["author"] = e.authorName;
        v["authorIcon"] = e.authorIcon.isEmpty() ? QString() : urls::provider(e.authorIcon, 24, 24, true);
        v["title"] = e.title;
        v["url"] = e.url;
        v["description"] = e.description.isEmpty() ? QString() : markdownToHtml(e.description, ctx);
        v["footer"] = e.footer;
        QVariantList fields;
        for (const auto &f : e.fields)
            fields.append(QVariantMap { { "name", markdownToHtml(f.name, ctx) }, { "value", markdownToHtml(f.value, ctx) }, { "inline", f.inlineField } });
        v["fields"] = fields;
        const EmbedMedia &img = mediaOnly && e.image.url.isEmpty() ? e.thumbnail : e.image;
        if (!img.url.isEmpty()) {
            const QSize s = fitSize(img.width, img.height, mediaOnly ? 400 : 380, 300);
            v["image"] = urls::media(img.proxyUrl, img.url, s.width(), s.height());
            v["imageFull"] = urls::media(img.proxyUrl, img.url, qMin(img.width > 0 ? img.width : 1600, 1600), qMin(img.height > 0 ? img.height : 1200, 1200));
            v["imageWidth"] = s.width();
            v["imageHeight"] = s.height();
        }
        if (!mediaOnly && !e.thumbnail.url.isEmpty()) {
            const QSize s = fitSize(e.thumbnail.width, e.thumbnail.height, 80, 80);
            v["thumbnail"] = urls::media(e.thumbnail.proxyUrl, e.thumbnail.url, s.width(), s.height());
            v["thumbWidth"] = s.width();
            v["thumbHeight"] = s.height();
        }
        out.append(v);
    }
    return out;
}

QVariantList MessageModel::reactions(const Message &m) const {
    QVariantList out;
    for (const auto &r : m.reactions) {
        QVariantMap v;
        v["count"] = r.count;
        v["me"] = r.me;
        v["emoji"] = r.emojiId ? QString() : r.emojiName;
        v["image"] = r.emojiId ? urls::provider(QString("https://cdn.discordapp.com/emojis/%1.%2?size=48").arg(r.emojiId).arg(r.animated && m_settings->animateImages() ? "gif" : "png"), 20, 20, false) : QString();
        v["key"] = r.emojiId ? QString("%1:%2").arg(r.emojiName).arg(r.emojiId) : r.emojiName;
        v["name"] = r.emojiName;
        out.append(v);
    }
    return out;
}

QVariantMap MessageModel::reply(const Message &m) const {
    if (!m.replyToId && !m.replyDeleted) return {};
    QVariantMap v;
    v["messageId"] = QString::number(m.replyToId);
    v["deleted"] = m.replyDeleted || !m.replyAuthorId;
    if (m.replyAuthorId) {
        const User *u = m_store->user(m.replyAuthorId);
        v["author"] = m_store->userDisplayName(m.replyAuthorId, m.guildId);
        const quint32 color = m_store->userColor(m.replyAuthorId, m.guildId);
        v["color"] = color ? QColor::fromRgb(color).name() : QString("#f2f3f5");
        v["avatar"] = u ? urls::avatar(*u, 16) : QString();
        auto ctx = markdownContext(m.guildId);
        QString plain = markdownToPlain(m.replyContent, ctx).replace('\n', ' ');
        if (plain.size() > 140) plain = plain.left(140) + "…";
        v["content"] = plain;
    }
    return v;
}

bool MessageModel::grouped(int row) const {
    if (row + 1 >= m_messages.size()) return false;
    const Message &m = m_messages[row];
    const Message &prev = m_messages[row + 1];
    if (m.replyToId || m.replyDeleted) return false;
    if (m.authorId != prev.authorId) return false;
    if (m.webhookId || prev.webhookId) return false;
    if (!systemText(m).isEmpty() || !systemText(prev).isEmpty()) return false;
    if (m.timestamp.toLocalTime().date() != prev.timestamp.toLocalTime().date()) return false;
    return prev.timestamp.secsTo(m.timestamp) < 7 * 60;
}

QVariant MessageModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_messages.size()) return {};
    const int row = index.row();
    const Message &m = m_messages[row];
    switch (role) {
        case IdRole: return QString::number(m.id);
        case AuthorIdRole: return QString::number(m.authorId);
        case AuthorNameRole: return m.webhookId && m_store->user(m.authorId) ? m_store->user(m.authorId)->displayName()
                                                                              : m_store->userDisplayName(m.authorId, m.guildId);
        case AuthorColorRole: {
            const quint32 c = m_store->userColor(m.authorId, m.guildId);
            return c ? QColor::fromRgb(c).name() : QString("#f2f3f5");
        }
        case AvatarRole: {
            const User *u = m_store->user(m.authorId);
            if (!u) return QString();
            const MemberInfo *mi = m.guildId ? m_store->member(m.guildId, m.authorId) : nullptr;
            return mi ? urls::guildAvatar(m.guildId, *u, mi->avatar, 40) : urls::avatar(*u, 40);
        }
        case TimeRole: return formatTime(m.timestamp);
        case FullTimeRole: return QLocale().toString(m.timestamp.toLocalTime(), "dddd, MMMM d, yyyy h:mm AP");
        case ShortTimeRole: return QLocale().toString(m.timestamp.toLocalTime().time(), "h:mm AP");
        case HtmlRole: return html(m);
        case PlainRole: return m.content;
        case GroupedRole: return grouped(row);
        case EditedRole: return m.edited.isValid();
        case AttachmentsRole: return attachments(m);
        case EmbedsRole: return embeds(m);
        case ReactionsRole: return reactions(m);
        case ReplyRole: return reply(m);
        case MentionsMeRole: return mentionsMe(m);
        case SystemRole: return systemText(m);
        case PendingRole: return m.pending;
        case FailedRole: return m.failed;
        case DayDividerRole: {
            if (row + 1 >= m_messages.size()) return m_hasMore ? QString() : QLocale().toString(m.timestamp.toLocalTime().date(), "MMMM d, yyyy");
            const QDate d = m.timestamp.toLocalTime().date();
            if (d != m_messages[row + 1].timestamp.toLocalTime().date()) return QLocale().toString(d, "MMMM d, yyyy");
            return QString();
        }
        case FirstUnreadRole: {
            if (!m_readMarker || m.id <= m_readMarker || m.authorId == m_store->self.id) return false;
            return row + 1 < m_messages.size() && m_messages[row + 1].id <= m_readMarker;
        }
        case IsBotRole: {
            const User *u = m_store->user(m.authorId);
            return (u && u->bot) || m.webhookId;
        }
        case IsSelfRole: return m.authorId == m_store->self.id;
        case StickersRole: return m.stickers.join(", ");
        case JumboRole: return isEmojiOnly(m.content);
    }
    return {};
}

} // namespace kestrel
