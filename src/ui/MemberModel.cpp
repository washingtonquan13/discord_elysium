#include "MemberModel.h"

#include "Urls.h"
#include "core/Json.h"

#include <QColor>
#include <QJsonArray>

namespace kestrel {

using namespace json;

MemberModel::MemberModel(Store *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store) {}

QHash<int, QByteArray> MemberModel::roleNames() const {
    return { { KindRole, "kind" }, { IdRole, "userId" }, { NameRole, "name" }, { ColorRole, "nameColor" }, { AvatarRole, "avatar" },
             { StatusRole, "status" }, { ActivityRole, "activity" }, { IsBotRole, "isBot" }, { CountRole, "count" } };
}

void MemberModel::showGuild(Snowflake guildId) {
    if (guildId == m_guild) return;
    beginResetModel();
    m_guild = guildId;
    m_listId.clear();
    m_rows.clear();
    endResetModel();
}

void MemberModel::showPrivate(Snowflake channelId) {
    beginResetModel();
    m_guild = 0;
    m_listId.clear();
    m_rows.clear();
    const Channel *c = m_store->channel(channelId);
    if (c && c->type == ChannelType::GroupDM) {
        QVector<Snowflake> ids = c->recipients;
        if (!ids.contains(m_store->self.id)) ids.prepend(m_store->self.id);
        Row header;
        header.group = true;
        header.groupId = "members";
        header.count = ids.size();
        m_rows.append(header);
        for (auto id : ids) {
            Row r;
            r.userId = id;
            r.status = id == m_store->self.id ? m_store->selfStatus : m_store->presence(id);
            m_rows.append(r);
        }
    }
    endResetModel();
}

MemberModel::Row MemberModel::parseItem(const QJsonObject &item) {
    Row r;
    if (has(item, "group")) {
        const auto g = obj(item, "group");
        r.group = true;
        r.groupId = str(g, "id");
        r.count = i32(g, "count");
        return r;
    }
    const auto m = obj(item, "member");
    m_store->storeMember(m_guild, m);
    r.userId = id(obj(m, "user"));
    const auto p = obj(m, "presence");
    r.status = str(p, "status", "offline");
    if (r.userId) m_store->presences.insert(r.userId, r.status);
    for (const auto &av : arr(p, "activities")) {
        const auto a = av.toObject();
        const int type = i32(a, "type");
        if (type == 4) { r.activity = str(a, "state"); break; } // custom status
        if (type == 0) { r.activity = "Playing " + str(a, "name"); break; }
        if (type == 1) { r.activity = "Streaming " + str(a, "details", str(a, "name")); break; }
        if (type == 2) { r.activity = "Listening to " + str(a, "name"); break; }
        if (type == 3) { r.activity = "Watching " + str(a, "name"); break; }
        if (type == 5) { r.activity = "Competing in " + str(a, "name"); break; }
    }
    return r;
}

void MemberModel::onListUpdate(Snowflake guildId, const QJsonObject &d) {
    if (guildId != m_guild) return;
    // Each channel permission set has its own list id; follow the most recently synced one.
    const QString listId = str(d, "id");
    bool isSync = false;
    for (const auto &opv : arr(d, "ops")) isSync |= str(opv.toObject(), "op") == "SYNC";
    if (!listId.isEmpty() && listId != m_listId) {
        if (!isSync) return;
        m_listId = listId;
        beginResetModel();
        m_rows.clear();
        endResetModel();
    }
    for (const auto &opv : arr(d, "ops")) {
        const auto op = opv.toObject();
        const QString kind = str(op, "op");
        if (kind == "SYNC") {
            const auto range = arr(op, "range");
            const int start = range.size() > 0 ? range[0].toInt() : 0;
            const auto items = arr(op, "items");
            if (start == 0) {
                beginResetModel();
                m_rows.clear();
                for (const auto &iv : items) m_rows.append(parseItem(iv.toObject()));
                endResetModel();
            } else {
                // later ranges: replace/extend from start (pad any hole first)
                if (start > m_rows.size()) {
                    beginInsertRows({}, m_rows.size(), start - 1);
                    while (m_rows.size() < start) m_rows.append(Row {});
                    endInsertRows();
                }
                for (int i = 0; i < items.size(); i++) {
                    const int idx = start + i;
                    Row r = parseItem(items[i].toObject());
                    if (idx < m_rows.size()) {
                        m_rows[idx] = r;
                        emit dataChanged(index(idx), index(idx));
                    } else {
                        beginInsertRows({}, m_rows.size(), m_rows.size());
                        m_rows.append(r);
                        endInsertRows();
                    }
                }
            }
        } else if (kind == "INSERT") {
            const int idx = qBound(0, i32(op, "index"), static_cast<int>(m_rows.size()));
            beginInsertRows({}, idx, idx);
            m_rows.insert(idx, parseItem(obj(op, "item")));
            endInsertRows();
        } else if (kind == "UPDATE") {
            const int idx = i32(op, "index");
            if (idx >= 0 && idx < m_rows.size()) {
                m_rows[idx] = parseItem(obj(op, "item"));
                emit dataChanged(index(idx), index(idx));
            }
        } else if (kind == "DELETE") {
            const int idx = i32(op, "index");
            if (idx >= 0 && idx < m_rows.size()) {
                beginRemoveRows({}, idx, idx);
                m_rows.remove(idx);
                endRemoveRows();
            }
        } else if (kind == "INVALIDATE") {
            // rows in this range are no longer tracked; keep them visible until the next SYNC
        }
    }
    // group headers carry the totals
    const auto groups = arr(d, "groups");
    for (int i = 0; i < m_rows.size(); i++) {
        if (!m_rows[i].group) continue;
        for (const auto &gv : groups) {
            const auto g = gv.toObject();
            if (str(g, "id") == m_rows[i].groupId && i32(g, "count") != m_rows[i].count) {
                m_rows[i].count = i32(g, "count");
                emit dataChanged(index(i), index(i), { CountRole });
            }
        }
    }
}

void MemberModel::refreshPresence(Snowflake userId) {
    for (int i = 0; i < m_rows.size(); i++) {
        if (m_rows[i].userId == userId) {
            m_rows[i].status = m_store->presence(userId);
            emit dataChanged(index(i), index(i), { StatusRole });
        }
    }
}

QVariant MemberModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row &r = m_rows[index.row()];
    if (r.group) {
        switch (role) {
            case KindRole: return QStringLiteral("group");
            case NameRole: {
                if (r.groupId == "online") return QStringLiteral("Online");
                if (r.groupId == "offline") return QStringLiteral("Offline");
                if (r.groupId == "members") return QStringLiteral("Members");
                const Guild *g = m_store->guild(m_guild);
                if (g) {
                    auto it = g->roles.find(r.groupId.toULongLong());
                    if (it != g->roles.end()) return it->name;
                }
                return r.groupId;
            }
            case CountRole: return r.count;
        }
        return {};
    }
    const User *u = m_store->user(r.userId);
    switch (role) {
        case KindRole: return QStringLiteral("member");
        case IdRole: return QString::number(r.userId);
        case NameRole: return m_store->userDisplayName(r.userId, m_guild);
        case ColorRole: {
            const quint32 c = m_guild ? m_store->userColor(r.userId, m_guild) : 0;
            return c ? QColor::fromRgb(c).name() : QString();
        }
        case AvatarRole: {
            if (!u) return QString();
            const MemberInfo *mi = m_guild ? m_store->member(m_guild, r.userId) : nullptr;
            return mi ? urls::guildAvatar(m_guild, *u, mi->avatar, 32) : urls::avatar(*u, 32);
        }
        case StatusRole: return r.status.isEmpty() ? QString("offline") : r.status;
        case ActivityRole: return r.activity;
        case IsBotRole: return u && u->bot;
    }
    return {};
}

} // namespace kestrel
