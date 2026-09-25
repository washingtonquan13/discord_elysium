#pragma once

#include "core/Client.h"
#include "core/RemoteAuth.h"
#include "core/Settings.h"
#include "core/TokenStore.h"
#include "ui/MemberModel.h"
#include "ui/MessageModel.h"
#include "ui/Models.h"

#include <QObject>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVariantMap>

class QQuickWindow;

namespace kestrel {

class AudioEngine;
class VoiceClient;

// The single object QML talks to: exposes models, current selection, and
// every user action. Keeps UI concerns out of the network/data layer.
class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString loginMessage READ loginMessage NOTIFY loginChanged)
    Q_PROPERTY(QString qrUrl READ qrUrl NOTIFY loginChanged)
    Q_PROPERTY(QString scannedName READ scannedName NOTIFY loginChanged)
    Q_PROPERTY(QString scannedAvatar READ scannedAvatar NOTIFY loginChanged)

    Q_PROPERTY(QString selfId READ selfId NOTIFY selfChanged)
    Q_PROPERTY(QString selfName READ selfName NOTIFY selfChanged)
    Q_PROPERTY(QString selfUsername READ selfUsername NOTIFY selfChanged)
    Q_PROPERTY(QString selfAvatar READ selfAvatar NOTIFY selfChanged)
    Q_PROPERTY(QString selfStatus READ selfStatus NOTIFY selfChanged)

    Q_PROPERTY(QString guildId READ guildId NOTIFY selectionChanged)
    Q_PROPERTY(QString guildName READ guildName NOTIFY selectionChanged)
    Q_PROPERTY(QString channelId READ channelId NOTIFY selectionChanged)
    Q_PROPERTY(QString channelName READ channelName NOTIFY selectionChanged)
    Q_PROPERTY(QString channelTopic READ channelTopic NOTIFY selectionChanged)
    Q_PROPERTY(int channelType READ channelType NOTIFY selectionChanged)
    Q_PROPERTY(bool canSend READ canSend NOTIFY selectionChanged)
    Q_PROPERTY(bool canAttach READ canAttach NOTIFY selectionChanged)
    Q_PROPERTY(bool canManageMessages READ canManageMessages NOTIFY selectionChanged)
    Q_PROPERTY(QString typingText READ typingText NOTIFY typingChanged)

    Q_PROPERTY(QVariantMap replyTo READ replyTo NOTIFY replyChanged)
    Q_PROPERTY(QString editingId READ editingId NOTIFY editingChanged)

    Q_PROPERTY(QString voiceState READ voiceState NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceChannelName READ voiceChannelName NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceChannelId READ voiceChannelId NOTIFY voiceChanged)
    Q_PROPERTY(bool voiceEncrypted READ voiceEncrypted NOTIFY voiceChanged)
    Q_PROPERTY(int voicePing READ voicePing NOTIFY voiceStatsChanged)
    Q_PROPERTY(bool selfMuted READ selfMuted NOTIFY voiceChanged)
    Q_PROPERTY(bool selfDeafened READ selfDeafened NOTIFY voiceChanged)
    Q_PROPERTY(double inputLevel READ inputLevel NOTIFY voiceStatsChanged)
    Q_PROPERTY(bool voiceAvailable READ voiceAvailable CONSTANT)
    Q_PROPERTY(QStringList inputDevices READ inputDevices NOTIFY devicesChanged)
    Q_PROPERTY(QStringList outputDevices READ outputDevices NOTIFY devicesChanged)

    Q_PROPERTY(QObject *guilds READ guilds CONSTANT)
    Q_PROPERTY(QObject *channels READ channels CONSTANT)
    Q_PROPERTY(QObject *dms READ dms CONSTANT)
    Q_PROPERTY(QObject *messages READ messages CONSTANT)
    Q_PROPERTY(QObject *members READ members CONSTANT)
    Q_PROPERTY(QObject *settings READ settings CONSTANT)

public:
    AppController(Settings *settings, QObject *parent = nullptr);
    ~AppController() override;

    void setWindow(QQuickWindow *w) { m_window = w; }
    void autoLogin();

    bool loggedIn() const { return m_loggedIn; }
    bool ready() const { return m_store->ready; }
    QString connectionState() const { return m_connState; }
    QString loginMessage() const { return m_loginMessage; }
    QString qrUrl() const { return m_qrUrl; }
    QString scannedName() const { return m_scannedName; }
    QString scannedAvatar() const { return m_scannedAvatar; }

    QString selfId() const { return QString::number(m_store->self.id); }
    QString selfName() const { return m_store->self.displayName(); }
    QString selfUsername() const { return m_store->self.username; }
    QString selfAvatar() const;
    QString selfStatus() const { return m_store->selfStatus; }

    QString guildId() const { return m_guild ? QString::number(m_guild) : QString(); }
    QString guildName() const;
    QString channelId() const { return m_channel ? QString::number(m_channel) : QString(); }
    QString channelName() const;
    QString channelTopic() const;
    int channelType() const;
    bool canSend() const;
    bool canAttach() const;
    bool canManageMessages() const;
    QString typingText() const { return m_typingText; }
    QVariantMap replyTo() const { return m_replyTo; }
    QString editingId() const { return m_editingId; }

    QString voiceState() const;
    QString voiceChannelName() const;
    QString voiceChannelId() const { return m_voiceChannel ? QString::number(m_voiceChannel) : QString(); }
    bool voiceEncrypted() const;
    int voicePing() const;
    bool selfMuted() const { return m_selfMuted; }
    bool selfDeafened() const { return m_selfDeafened; }
    double inputLevel() const;
    bool voiceAvailable() const;
    QStringList inputDevices() const { return m_inputDevices; }
    QStringList outputDevices() const { return m_outputDevices; }

    QObject *guilds() { return m_guildModel; }
    QObject *channels() { return m_channelModel; }
    QObject *dms() { return m_dmModel; }
    QObject *messages() { return m_messageModel; }
    QObject *members() { return m_memberModel; }
    QObject *settings() { return m_settings; }

    Q_INVOKABLE void loginWithToken(const QString &token);
    Q_INVOKABLE void startQrLogin();
    Q_INVOKABLE void stopQrLogin();
    Q_INVOKABLE void logout();

    Q_INVOKABLE void selectGuild(const QString &id);
    Q_INVOKABLE void selectChannel(const QString &id);
    Q_INVOKABLE void openDmWith(const QString &userId);

    Q_INVOKABLE void sendMessage(const QString &text, const QStringList &files = {});
    Q_INVOKABLE void retryMessage(const QString &messageId);
    Q_INVOKABLE void startReply(const QString &messageId, const QString &authorName);
    Q_INVOKABLE void setReplyMention(bool mention);
    Q_INVOKABLE void cancelReply();
    Q_INVOKABLE void startEdit(const QString &messageId);
    Q_INVOKABLE void cancelEdit();
    Q_INVOKABLE void submitEdit(const QString &text);
    Q_INVOKABLE void deleteMessage(const QString &messageId);
    Q_INVOKABLE void toggleReaction(const QString &messageId, const QString &key, bool me);
    Q_INVOKABLE void userTyping();
    Q_INVOKABLE void handleLink(const QString &url);
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void setStatus(const QString &status);
    Q_INVOKABLE void markCurrentRead();
    Q_INVOKABLE QVariantMap userProfile(const QString &userId);
    Q_INVOKABLE void requestMemberList(int visibleEnd);

    Q_INVOKABLE void joinVoice(const QString &channelId);
    Q_INVOKABLE void leaveVoice();
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void toggleDeafen();
    Q_INVOKABLE void setUserVolume(const QString &userId, double volume);
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void startMicTest();
    Q_INVOKABLE void stopMicTest();
    Q_INVOKABLE void applyVoiceSettings();

    Q_INVOKABLE QString keyName(int qtKey, int nativeVk, const QString &text) const;
    Q_INVOKABLE QVariantList emojiRows(const QString &filter, int columns);
    Q_INVOKABLE QVariantList autocomplete(const QString &trigger, const QString &query);
    Q_INVOKABLE QVariantList quickSwitch(const QString &query);
    Q_INVOKABLE void screenshot(const QString &path);
    Q_INVOKABLE QString memoryUsage() const;

signals:
    void loggedInChanged();
    void readyChanged();
    void connectionStateChanged();
    void loginChanged();
    void selfChanged();
    void selectionChanged();
    void typingChanged();
    void replyChanged();
    void editingChanged();
    void voiceChanged();
    void voiceStatsChanged();
    void devicesChanged();
    void toast(const QString &text);
    void focusComposer();
    void editTextRequested(const QString &text);

private:
    void wireStore();
    void onReady();
    void onToken(const QString &token);
    void updateTyping();
    void onMessageCreated(const Message &m);
    void onVoiceStateChanged(const VoiceState &vs, Snowflake previousChannel);
    void notify(const QString &title, const QString &body);
    void setupTray();

    Settings *m_settings;
    Client *m_client;
    Store *m_store;
    TokenStore m_tokens;
    RemoteAuth *m_remoteAuth = nullptr;
    GuildListModel *m_guildModel;
    ChannelListModel *m_channelModel;
    DmListModel *m_dmModel;
    MessageModel *m_messageModel;
    MemberModel *m_memberModel;
    QQuickWindow *m_window = nullptr;
    QSystemTrayIcon *m_tray = nullptr;

    bool m_loggedIn = false;
    QString m_connState = "disconnected";
    QString m_loginMessage;
    QString m_qrUrl;
    QString m_scannedName;
    QString m_scannedAvatar;

    Snowflake m_guild = 0;
    Snowflake m_channel = 0;
    QHash<Snowflake, Snowflake> m_lastChannelInGuild;
    QString m_typingText;
    QTimer m_typingTimer;
    QVariantMap m_replyTo;
    QString m_editingId;
    QHash<QString, QStringList> m_failedFiles;

    // voice
    AudioEngine *m_audio = nullptr;
    VoiceClient *m_voice = nullptr;
    Snowflake m_voiceGuild = 0;
    Snowflake m_voiceChannel = 0;
    bool m_selfMuted = false;
    bool m_selfDeafened = false;
    QTimer m_voicePoll;
    QStringList m_inputDevices;
    QStringList m_outputDevices;
    bool m_micTest = false;
};

} // namespace kestrel
