#pragma once

#include "core/Store.h"

#include <QAbstractListModel>
#include <QJsonObject>

namespace kestrel {

// Right-hand member sidebar. For servers it mirrors Discord's lazily synced
// member list (GUILD_MEMBER_LIST_UPDATE); for group DMs it lists recipients.
class MemberModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles { KindRole = Qt::UserRole + 1, IdRole, NameRole, ColorRole, AvatarRole, StatusRole, ActivityRole, IsBotRole, CountRole };

    explicit MemberModel(Store *store, QObject *parent = nullptr);
    int rowCount(const QModelIndex &) const override { return m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void showGuild(Snowflake guildId);
    void showPrivate(Snowflake channelId);
    void onListUpdate(Snowflake guildId, const QJsonObject &d);
    void refreshPresence(Snowflake userId);

private:
    struct Row {
        bool group = false;
        QString groupId;
        int count = 0;
        Snowflake userId = 0;
        QString status;
        QString activity;
    };
    Row parseItem(const QJsonObject &item);

    Store *m_store;
    Snowflake m_guild = 0;
    QString m_listId;
    QVector<Row> m_rows;
};

} // namespace kestrel
