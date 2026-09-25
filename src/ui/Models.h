#pragma once

#include "core/Store.h"

#include <QAbstractListModel>
#include <QSet>

namespace kestrel {

class Settings;

// Left-most column: Home, then servers (optionally grouped into folders).
class GuildListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles { KindRole = Qt::UserRole + 1, IdRole, NameRole, IconRole, InitialsRole, UnreadRole, MentionsRole,
                 FolderColorRole, InFolderRole, ExpandedRole, FolderIconsRole };

    GuildListModel(Store *store, QObject *parent = nullptr);
    int rowCount(const QModelIndex &) const override { return m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void toggleFolder(const QString &folderId);
    void rebuild();
    void refreshIndicators();
    void refreshGuild(Snowflake guildId); // 0 = home (DMs)

private:
    struct Row {
        QString kind; // home, separator, guild, folder
        Snowflake id = 0;
        qint64 folderId = 0;
        bool inFolder = false;
        qint64 color = -1;
        QString name;
        QVector<Snowflake> folderGuilds;
    };
    Store *m_store;
    QVector<Row> m_rows;
    QSet<qint64> m_expanded;
};

// Channels of the selected server, flattened with category headers.
class ChannelListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles { KindRole = Qt::UserRole + 1, IdRole, NameRole, TypeRole, UnreadRole, MentionsRole, MutedRole,
                 CollapsedRole, VoiceMembersRole, NsfwRole, LimitRole };

    ChannelListModel(Store *store, Settings *settings, QObject *parent = nullptr);
    int rowCount(const QModelIndex &) const override { return m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setGuild(Snowflake guildId);
    void setSelected(Snowflake channelId) { m_selected = channelId; }
    Snowflake guildId() const { return m_guild; }
    Q_INVOKABLE void toggleCategory(const QString &id);
    void rebuild();
    void refreshChannel(Snowflake channelId);
    void refreshVoice();
    void setSpeaking(const QSet<Snowflake> &speaking);
    Snowflake firstTextChannel() const;

private:
    QVariantList voiceMembers(Snowflake channelId) const;

    Store *m_store;
    Settings *m_settings;
    Snowflake m_guild = 0;
    Snowflake m_selected = 0;
    QSet<Snowflake> m_speaking;
    struct Row {
        bool category = false;
        Snowflake id = 0;
    };
    QVector<Row> m_rows;
};

// Direct messages and group DMs, most recent first.
class DmListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, AvatarRole, StatusRole, UnreadRole, MentionsRole, IsGroupRole, SubtitleRole };

    explicit DmListModel(Store *store, QObject *parent = nullptr);
    int rowCount(const QModelIndex &) const override { return m_rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void rebuild();
    void refreshChannel(Snowflake channelId);
    void refreshPresence(Snowflake userId);

private:
    Store *m_store;
    QVector<Snowflake> m_rows;
};

} // namespace kestrel
