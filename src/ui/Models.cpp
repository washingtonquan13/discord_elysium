#include "Models.h"

#include "Urls.h"
#include "core/Permissions.h"
#include "core/Settings.h"

#include <QColor>
#include <algorithm>

namespace kestrel {

// ------------------------------------------------------------------ guilds

GuildListModel::GuildListModel(Store *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store) {}

QHash<int, QByteArray> GuildListModel::roleNames() const {
    return { { KindRole, "kind" }, { IdRole, "itemId" }, { NameRole, "name" }, { IconRole, "icon" }, { InitialsRole, "initials" },
             { UnreadRole, "unread" }, { MentionsRole, "mentions" }, { FolderColorRole, "folderColor" }, { InFolderRole, "inFolder" },
             { ExpandedRole, "expanded" }, { FolderIconsRole, "folderIcons" } };
}

void GuildListModel::rebuild() {
    beginResetModel();
    m_rows.clear();
    m_rows.append({ "home" });
    m_rows.append({ "separator" });

    QSet<Snowflake> inLayout;
    for (const auto &f : m_store->folders)
        for (auto g : f.guildIds) inLayout.insert(g);

    for (auto gid : m_store->guildOrder) {
        if (!inLayout.contains(gid) && m_store->guild(gid)) m_rows.append({ "guild", gid });
    }
    for (const auto &f : m_store->folders) {
        QVector<Snowflake> present;
        for (auto g : f.guildIds)
            if (m_store->guild(g)) present.append(g);
        if (present.isEmpty()) continue;
        if (f.id == 0 || (present.size() == 1 && f.name.isEmpty())) {
            for (auto g : present) m_rows.append({ "guild", g });
            continue;
        }
        Row folder { "folder", 0, f.id, false, f.color, f.name, present };
        m_rows.append(folder);
        if (m_expanded.contains(f.id)) {
            for (auto g : present) m_rows.append({ "guild", g, f.id, true, f.color });
        }
    }
    endResetModel();
}

void GuildListModel::refreshIndicators() {
    if (m_rows.isEmpty()) return;
    emit dataChanged(index(0), index(m_rows.size() - 1), { UnreadRole, MentionsRole, NameRole, IconRole });
}

void GuildListModel::refreshGuild(Snowflake guildId) {
    for (int i = 0; i < m_rows.size(); i++) {
        const Row &r = m_rows[i];
        const bool hit = guildId == 0 ? r.kind == "home"
                                      : (r.kind == "guild" && r.id == guildId) || (r.kind == "folder" && r.folderGuilds.contains(guildId));
        if (hit) emit dataChanged(index(i), index(i), { UnreadRole, MentionsRole });
    }
}

void GuildListModel::toggleFolder(const QString &folderId) {
    const qint64 id = folderId.toLongLong();
    if (m_expanded.contains(id)) m_expanded.remove(id);
    else m_expanded.insert(id);
    rebuild();
}

QVariant GuildListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row &r = m_rows[index.row()];
    switch (role) {
        case KindRole: return r.kind;
        case IdRole: return r.kind == "folder" ? QString::number(r.folderId) : QString::number(r.id);
        case InFolderRole: return r.inFolder;
        case ExpandedRole: return m_expanded.contains(r.folderId);
        case FolderColorRole: return r.color >= 0 ? QColor::fromRgb(static_cast<QRgb>(r.color)).name() : QString("#5865f2");
        default: break;
    }
    if (r.kind == "guild") {
        const Guild *g = m_store->guild(r.id);
        if (!g) return {};
        switch (role) {
            case NameRole: return g->name;
            case IconRole: return urls::guildIcon(*g, 48);
            case InitialsRole: return urls::initials(g->name);
            case UnreadRole: return m_store->guildUnread(g->id);
            case MentionsRole: return m_store->guildMentions(g->id);
        }
    } else if (r.kind == "folder") {
        switch (role) {
            case NameRole: {
                if (!r.name.isEmpty()) return r.name;
                QStringList names;
                for (auto gid : r.folderGuilds)
                    if (auto *g = m_store->guild(gid)) names.append(g->name);
                return names.join(", ");
            }
            case FolderIconsRole: {
                QVariantList icons;
                for (int i = 0; i < r.folderGuilds.size() && i < 4; i++) {
                    auto *g = m_store->guild(r.folderGuilds[i]);
                    QVariantMap m;
                    m["icon"] = g ? urls::guildIcon(*g, 20) : QString();
                    m["initials"] = g ? urls::initials(g->name) : QString();
                    icons.append(m);
                }
                return icons;
            }
            case UnreadRole: return std::any_of(r.folderGuilds.begin(), r.folderGuilds.end(), [this](Snowflake g) { return m_store->guildUnread(g); });
            case MentionsRole: {
                int n = 0;
                for (auto g : r.folderGuilds) n += m_store->guildMentions(g);
                return n;
            }
        }
    } else if (r.kind == "home") {
        if (role == MentionsRole) {
            int n = 0;
            for (auto c : m_store->privateChannels) n += m_store->mentionCount(c);
            return n;
        }
        if (role == NameRole) return QStringLiteral("Direct Messages");
    }
    return {};
}

// ------------------------------------------------------------------ channels

ChannelListModel::ChannelListModel(Store *store, Settings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
    , m_settings(settings) {}

QHash<int, QByteArray> ChannelListModel::roleNames() const {
    return { { KindRole, "kind" }, { IdRole, "itemId" }, { NameRole, "name" }, { TypeRole, "channelType" }, { UnreadRole, "unread" },
             { MentionsRole, "mentions" }, { MutedRole, "muted" }, { CollapsedRole, "collapsed" }, { VoiceMembersRole, "voiceMembers" },
             { NsfwRole, "nsfw" }, { LimitRole, "userLimit" } };
}

void ChannelListModel::setGuild(Snowflake guildId) {
    m_guild = guildId;
    rebuild();
}

static int typeRank(ChannelType t) {
    switch (t) {
        case ChannelType::GuildVoice:
        case ChannelType::GuildStageVoice: return 1;
        default: return 0;
    }
}

void ChannelListModel::rebuild() {
    beginResetModel();
    m_rows.clear();
    const Guild *g = m_store->guild(m_guild);
    if (g) {
        QVector<const Channel *> top, categories;
        QHash<Snowflake, QVector<const Channel *>> children;
        for (auto cid : g->channels) {
            const Channel *c = m_store->channel(cid);
            if (!c) continue;
            if (c->type == ChannelType::GuildCategory) {
                categories.append(c);
                continue;
            }
            if (c->type != ChannelType::GuildText && c->type != ChannelType::GuildNews && c->type != ChannelType::GuildVoice
                && c->type != ChannelType::GuildStageVoice && c->type != ChannelType::GuildForum && c->type != ChannelType::GuildMedia)
                continue;
            if (!m_store->canView(cid)) continue;
            if (c->parentId) children[c->parentId].append(c);
            else top.append(c);
        }
        auto byPos = [](const Channel *a, const Channel *b) {
            if (typeRank(a->type) != typeRank(b->type)) return typeRank(a->type) < typeRank(b->type);
            if (a->position != b->position) return a->position < b->position;
            return a->id < b->id;
        };
        std::sort(top.begin(), top.end(), byPos);
        std::sort(categories.begin(), categories.end(), [](const Channel *a, const Channel *b) {
            return a->position != b->position ? a->position < b->position : a->id < b->id;
        });
        for (auto *c : top) m_rows.append({ false, c->id });
        for (auto *cat : categories) {
            auto kids = children.value(cat->id);
            if (kids.isEmpty()) continue;
            std::sort(kids.begin(), kids.end(), byPos);
            m_rows.append({ true, cat->id });
            const bool collapsed = m_settings->categoryCollapsed(QString::number(cat->id));
            for (auto *c : kids) {
                // collapsed categories still show the selected channel and occupied voice channels
                if (collapsed && c->id != m_selected && !(c->isVoice() && !m_store->voiceStatesIn(c->id).isEmpty())) continue;
                m_rows.append({ false, c->id });
            }
        }
    }
    endResetModel();
}

void ChannelListModel::toggleCategory(const QString &id) {
    m_settings->setCategoryCollapsed(id, !m_settings->categoryCollapsed(id));
    rebuild();
}

void ChannelListModel::refreshChannel(Snowflake channelId) {
    for (int i = 0; i < m_rows.size(); i++) {
        if (m_rows[i].id == channelId) {
            emit dataChanged(index(i), index(i), { UnreadRole, MentionsRole, NameRole, MutedRole });
            return;
        }
    }
}

void ChannelListModel::refreshVoice() {
    for (int i = 0; i < m_rows.size(); i++) {
        if (m_rows[i].category) continue;
        const Channel *c = m_store->channel(m_rows[i].id);
        if (c && c->isVoice()) emit dataChanged(index(i), index(i), { VoiceMembersRole });
    }
}

void ChannelListModel::setSpeaking(const QSet<Snowflake> &speaking) {
    if (speaking == m_speaking) return;
    m_speaking = speaking;
    refreshVoice();
}

Snowflake ChannelListModel::firstTextChannel() const {
    for (const auto &r : m_rows) {
        if (r.category) continue;
        const Channel *c = m_store->channel(r.id);
        if (c && (c->type == ChannelType::GuildText || c->type == ChannelType::GuildNews)) return r.id;
    }
    return 0;
}

QVariantList ChannelListModel::voiceMembers(Snowflake channelId) const {
    QVariantList out;
    for (const auto &vs : m_store->voiceStatesIn(channelId)) {
        QVariantMap m;
        const User *u = m_store->user(vs.userId);
        m["userId"] = QString::number(vs.userId);
        m["name"] = m_store->userDisplayName(vs.userId, vs.guildId);
        const MemberInfo *mi = m_store->member(vs.guildId, vs.userId);
        m["avatar"] = u ? (mi ? urls::guildAvatar(vs.guildId, *u, mi->avatar, 24) : urls::avatar(*u, 24)) : QString();
        m["muted"] = vs.selfMute || vs.mute;
        m["deafened"] = vs.selfDeaf || vs.deaf;
        m["streaming"] = vs.selfStream;
        m["speaking"] = m_speaking.contains(vs.userId);
        out.append(m);
    }
    return out;
}

QVariant ChannelListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row &r = m_rows[index.row()];
    const Channel *c = m_store->channel(r.id);
    if (!c) return {};
    switch (role) {
        case KindRole: return r.category ? QStringLiteral("category") : QStringLiteral("channel");
        case IdRole: return QString::number(c->id);
        case NameRole: return c->name;
        case TypeRole: return static_cast<int>(c->type);
        case UnreadRole: return !r.category && c->isText() && !m_store->isChannelMuted(c->id) && m_store->isUnread(c->id);
        case MentionsRole: return r.category ? 0 : m_store->mentionCount(c->id);
        case MutedRole: return m_store->isChannelMuted(c->id);
        case CollapsedRole: return r.category && m_settings->categoryCollapsed(QString::number(c->id));
        case VoiceMembersRole: return c->isVoice() ? voiceMembers(c->id) : QVariantList();
        case NsfwRole: return c->nsfw;
        case LimitRole: return c->userLimit;
    }
    return {};
}

// ------------------------------------------------------------------ DMs

DmListModel::DmListModel(Store *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store) {}

QHash<int, QByteArray> DmListModel::roleNames() const {
    return { { IdRole, "itemId" }, { NameRole, "name" }, { AvatarRole, "avatar" }, { StatusRole, "status" }, { UnreadRole, "unread" },
             { MentionsRole, "mentions" }, { IsGroupRole, "isGroup" }, { SubtitleRole, "subtitle" } };
}

void DmListModel::rebuild() {
    beginResetModel();
    m_rows = m_store->sortedPrivateChannels();
    endResetModel();
}

void DmListModel::refreshChannel(Snowflake channelId) {
    const int i = m_rows.indexOf(channelId);
    if (i >= 0) emit dataChanged(index(i), index(i), { UnreadRole, MentionsRole, NameRole });
}

void DmListModel::refreshPresence(Snowflake userId) {
    for (int i = 0; i < m_rows.size(); i++) {
        const Channel *c = m_store->channel(m_rows[i]);
        if (c && c->type == ChannelType::DM && c->recipients.contains(userId)) emit dataChanged(index(i), index(i), { StatusRole });
    }
}

QVariant DmListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Channel *c = m_store->channel(m_rows[index.row()]);
    if (!c) return {};
    const bool group = c->type == ChannelType::GroupDM;
    Snowflake other = 0;
    for (auto r : c->recipients)
        if (r != m_store->self.id) { other = r; break; }
    switch (role) {
        case IdRole: return QString::number(c->id);
        case NameRole: return m_store->privateChannelName(*c);
        case AvatarRole: {
            if (group) return urls::channelIcon(*c, 32);
            const User *u = m_store->user(other);
            return u ? urls::avatar(*u, 32) : QString();
        }
        case StatusRole: return group ? QString() : m_store->presence(other);
        case UnreadRole: return m_store->isUnread(c->id);
        case MentionsRole: return m_store->mentionCount(c->id);
        case IsGroupRole: return group;
        case SubtitleRole: return group ? QString("%1 Members").arg(c->recipients.size() + 1) : QString();
    }
    return {};
}

} // namespace kestrel
