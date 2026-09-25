#pragma once

#include "core/Client.h"
#include "core/Markdown.h"

#include <QAbstractListModel>
#include <QHash>

namespace kestrel {

class Settings;

// Messages of the open channel, newest first (row 0 is at the bottom of the
// view). Recently visited channels are kept in a small LRU so switching back
// is instant, and rendered HTML is cached per message.
class MessageModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY loadingChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1, AuthorIdRole, AuthorNameRole, AuthorColorRole, AvatarRole, TimeRole, FullTimeRole, ShortTimeRole,
        HtmlRole, PlainRole, GroupedRole, EditedRole, AttachmentsRole, EmbedsRole, ReactionsRole, ReplyRole, MentionsMeRole,
        SystemRole, PendingRole, FailedRole, DayDividerRole, FirstUnreadRole, IsBotRole, IsSelfRole, StickersRole, JumboRole
    };

    MessageModel(Client *client, Settings *settings, QObject *parent = nullptr);

    int rowCount(const QModelIndex &) const override { return m_messages.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setChannel(Snowflake channelId);
    Snowflake channelId() const { return m_channel; }
    bool loading() const { return m_loading; }
    bool hasMore() const { return m_hasMore; }

    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void jumpTo(const QString &messageId);
    Q_INVOKABLE QString rawContent(const QString &messageId) const;
    Q_INVOKABLE QString lastOwnMessageId() const;

    void addPending(const Message &m);
    void invalidate(); // drop cached channels (after a fresh READY or logout)
    void markFailed(const QString &nonce);
    void refreshAuthors();
    Snowflake newestId() const { return m_messages.isEmpty() ? 0 : m_messages.first().id; }

signals:
    void loadingChanged();
    void positionRequested(int row);
    void newMessageArrived(bool fromSelf);

private:
    void onCreated(const Message &m);
    void onUpdated(Snowflake channelId, Snowflake id, const QJsonObject &d);
    void onDeleted(Snowflake channelId, Snowflake id);
    void onReaction(Snowflake channelId, Snowflake messageId, const QJsonObject &emoji, int delta, bool me);
    void onReactionsCleared(Snowflake channelId, Snowflake messageId);
    void requestAuthors(const QVector<Message> &msgs);
    void cacheCurrent();
    int rowOf(Snowflake id) const;
    QString html(const Message &m) const;
    MarkdownContext markdownContext(Snowflake guildId) const;
    QString systemText(const Message &m) const;
    QVariantList attachments(const Message &m) const;
    QVariantList embeds(const Message &m) const;
    QVariantList reactions(const Message &m) const;
    QVariantMap reply(const Message &m) const;
    bool grouped(int row) const;
    bool mentionsMe(const Message &m) const;

    Client *m_client;
    Store *m_store;
    Settings *m_settings;
    Snowflake m_channel = 0;
    Snowflake m_guild = 0;
    Snowflake m_readMarker = 0;
    QVector<Message> m_messages;
    bool m_loading = false;
    bool m_hasMore = true;
    int m_generation = 0;
    mutable QHash<Snowflake, QString> m_html;

    struct Cached {
        QVector<Message> messages;
        bool hasMore = true;
    };
    QHash<Snowflake, Cached> m_cache;
    QVector<Snowflake> m_lru;
};

} // namespace kestrel
