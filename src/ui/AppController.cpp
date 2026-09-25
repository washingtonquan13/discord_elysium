#include "AppController.h"

#include "Urls.h"
#include "core/Emoji.h"
#include "core/Json.h"
#include "core/Markdown.h"
#include "core/Permissions.h"

#ifdef KESTREL_VOICE
    #include "voice/AudioEngine.h"
    #include "voice/VoiceClient.h"
#endif

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QJsonArray>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMenu>
#include <QQuickWindow>
#include <QUrl>

namespace kestrel {

using namespace json;

AppController::AppController(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_client(new Client(urls::endpoints(), this))
    , m_store(m_client->store())
    , m_tokens(settings->dataDir()) {
    m_guildModel = new GuildListModel(m_store, this);
    m_channelModel = new ChannelListModel(m_store, m_settings, this);
    m_dmModel = new DmListModel(m_store, this);
    m_messageModel = new MessageModel(m_client, m_settings, this);
    m_memberModel = new MemberModel(m_store, this);

    connect(m_client, &Client::connectionStateChanged, this, [this](const QString &s) {
        m_connState = s;
        emit connectionStateChanged();
    });
    connect(m_client, &Client::loggedOut, this, [this](const QString &reason) {
        logout();
        m_loginMessage = reason;
        emit loginChanged();
    });
    connect(m_client, &Client::tokenChanged, this, [this](const QString &t) { m_tokens.save(t); });

    m_typingTimer.setInterval(1000);
    connect(&m_typingTimer, &QTimer::timeout, this, &AppController::updateTyping);

    wireStore();

#ifdef KESTREL_VOICE
    m_audio = new AudioEngine();
    m_voice = new VoiceClient(m_audio, m_settings, this);
    connect(m_voice, &VoiceClient::stateChanged, this, [this](const QString &s) {
        if (s == "error") emit toast("Voice connection failed.");
        emit voiceChanged();
    });
    connect(m_voice, &VoiceClient::encryptionChanged, this, &AppController::voiceChanged);
    m_voicePoll.setInterval(100);
    connect(&m_voicePoll, &QTimer::timeout, this, [this]() {
        m_channelModel->setSpeaking(m_voice->speakingUsers());
        emit voiceStatsChanged();
    });
#endif
    setupTray();
}

AppController::~AppController() {
#ifdef KESTREL_VOICE
    delete m_voice;
    m_voice = nullptr;
    delete m_audio;
#endif
}

void AppController::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    m_tray = new QSystemTrayIcon(QIcon(":/icons/app.png"), this);
    m_tray->setToolTip("Kestrel");
    auto *menu = new QMenu();
    menu->addAction("Open Kestrel", this, [this]() {
        if (m_window) {
            m_window->show();
            m_window->raise();
            m_window->requestActivate();
        }
    });
    menu->addSeparator();
    menu->addAction("Quit", qApp, &QCoreApplication::quit);
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger && m_window) {
            m_window->show();
            m_window->raise();
            m_window->requestActivate();
        }
    });
    m_tray->show();
}

void AppController::wireStore() {
    connect(m_store, &Store::readyReceived, this, &AppController::onReady);
    connect(m_store, &Store::guildsChanged, this, [this]() {
        m_guildModel->rebuild();
        emit selectionChanged();
    });
    connect(m_store, &Store::guildChanged, this, [this](Snowflake) {
        m_guildModel->refreshIndicators();
        emit selectionChanged();
    });
    connect(m_store, &Store::channelsChanged, this, [this](Snowflake g) {
        if (g == m_guild) m_channelModel->rebuild();
        m_guildModel->refreshIndicators();
        emit selectionChanged();
    });
    connect(m_store, &Store::privateChannelsChanged, this, [this]() { m_dmModel->rebuild(); });
    connect(m_store, &Store::unreadChanged, this, [this](Snowflake c) {
        m_channelModel->refreshChannel(c);
        m_dmModel->refreshChannel(c);
        const Channel *ch = m_store->channel(c);
        m_guildModel->refreshGuild(ch ? ch->guildId : 0);
    });
    connect(m_store, &Store::typingChanged, this, [this](Snowflake c) {
        if (c == m_channel) updateTyping();
    });
    connect(m_store, &Store::messageCreated, this, &AppController::onMessageCreated);
    connect(m_store, &Store::presenceChanged, this, [this](Snowflake u) {
        m_dmModel->refreshPresence(u);
        m_memberModel->refreshPresence(u);
    });
    connect(m_store, &Store::memberListUpdate, m_memberModel, &MemberModel::onListUpdate);
    connect(m_store, &Store::selfChanged, this, &AppController::selfChanged);
    connect(m_store, &Store::voiceStateChanged, this, &AppController::onVoiceStateChanged);
    connect(m_store, &Store::voiceServerUpdate, this, [this](Snowflake guildId, const QString &endpoint, const QString &token) {
#ifdef KESTREL_VOICE
        if (!m_voiceChannel) return;
        m_voice->setServer(guildId, m_voiceChannel, endpoint, token);
        m_voicePoll.start();
        emit voiceChanged();
#else
        Q_UNUSED(guildId) Q_UNUSED(endpoint) Q_UNUSED(token)
#endif
    });
}

void AppController::autoLogin() {
    const QString token = m_tokens.load();
    if (!token.isEmpty()) loginWithToken(token);
}

void AppController::loginWithToken(const QString &raw) {
    QString token = raw.trimmed();
    if (token.startsWith('"') && token.endsWith('"')) token = token.mid(1, token.size() - 2);
    if (token.isEmpty()) return;
    stopQrLogin();
    m_tokens.save(token);
    m_loggedIn = true;
    m_loginMessage.clear();
    emit loggedInChanged();
    emit loginChanged();
    m_client->start(token);
}

void AppController::startQrLogin() {
    if (!m_remoteAuth) {
        m_remoteAuth = new RemoteAuth(urls::endpoints().remoteAuth, m_client->http(), this);
        connect(m_remoteAuth, &RemoteAuth::qrUrlChanged, this, [this](const QString &u) {
            m_qrUrl = u;
            m_scannedName.clear();
            m_scannedAvatar.clear();
            m_loginMessage.clear();
            emit loginChanged();
        });
        connect(m_remoteAuth, &RemoteAuth::userScanned, this, [this](const QString &name, const QString &avatar) {
            m_scannedName = name;
            m_scannedAvatar = avatar.isEmpty() ? QString() : urls::provider(avatar, 80, 80, true);
            emit loginChanged();
        });
        connect(m_remoteAuth, &RemoteAuth::tokenReceived, this, &AppController::loginWithToken);
        connect(m_remoteAuth, &RemoteAuth::failed, this, [this](const QString &reason) {
            m_loginMessage = reason;
            m_scannedName.clear();
            emit loginChanged();
        });
    }
    m_qrUrl.clear();
    emit loginChanged();
    // The login handshake needs Discord's cookies/build number like any request.
    m_client->http()->bootstrap([this]() {
        if (m_remoteAuth && !m_loggedIn) m_remoteAuth->start();
    });
}

void AppController::stopQrLogin() {
    if (m_remoteAuth) m_remoteAuth->stop();
}

void AppController::logout() {
    leaveVoice();
    m_client->stop();
    m_tokens.clear();
    m_loggedIn = false;
    m_guild = m_channel = 0;
    m_messageModel->setChannel(0);
    m_messageModel->invalidate();
    m_channelModel->setGuild(0);
    m_guildModel->rebuild();
    m_dmModel->rebuild();
    emit loggedInChanged();
    emit readyChanged();
    emit selectionChanged();
}

void AppController::onReady() {
    m_client->resetMemberRequests();
    m_messageModel->invalidate();
    m_guildModel->rebuild();
    m_dmModel->rebuild();
    emit readyChanged();
    emit selfChanged();

    const Snowflake lastGuild = m_settings->value("last/guild").toULongLong();
    const Snowflake lastChannel = m_settings->value("last/channel").toULongLong();
    if (lastGuild && m_store->guild(lastGuild)) {
        m_lastChannelInGuild[lastGuild] = lastChannel;
        selectGuild(QString::number(lastGuild));
    } else {
        selectGuild({});
        if (lastChannel && m_store->channel(lastChannel)) selectChannel(QString::number(lastChannel));
    }
}

QString AppController::selfAvatar() const {
    return urls::avatar(m_store->self, 32);
}

QString AppController::guildName() const {
    const Guild *g = m_store->guild(m_guild);
    return g ? g->name : QString();
}

QString AppController::channelName() const {
    const Channel *c = m_store->channel(m_channel);
    if (!c) return {};
    return c->isPrivate() ? m_store->privateChannelName(*c) : c->name;
}

QString AppController::channelTopic() const {
    const Channel *c = m_store->channel(m_channel);
    if (!c) return {};
    if (c->type == ChannelType::DM) {
        for (auto r : c->recipients)
            if (r != m_store->self.id)
                if (auto *u = m_store->user(r)) return u->username;
    }
    MarkdownContext ctx;
    ctx.channelName = [this](Snowflake id) { auto *ch = m_store->channel(id); return ch ? ch->name : QString(); };
    ctx.userName = [this](Snowflake id) { return m_store->userDisplayName(id, m_guild); };
    return markdownToPlain(c->topic, ctx).replace('\n', ' ');
}

int AppController::channelType() const {
    const Channel *c = m_store->channel(m_channel);
    return c ? static_cast<int>(c->type) : -1;
}

bool AppController::canSend() const {
    return m_channel && (m_store->permissions(m_channel) & perm::SendMessages);
}

bool AppController::canAttach() const {
    return m_channel && (m_store->permissions(m_channel) & perm::AttachFiles);
}

bool AppController::canManageMessages() const {
    return m_channel && (m_store->permissions(m_channel) & perm::ManageMessages) && m_guild;
}

void AppController::selectGuild(const QString &id) {
    const Snowflake gid = id.toULongLong();
    m_guild = gid;
    m_settings->set("last/guild", QString::number(gid));
    m_channelModel->setGuild(gid);
    cancelReply();
    cancelEdit();
    if (gid) {
        Snowflake target = m_lastChannelInGuild.value(gid);
        if (!target || !m_store->channel(target) || !m_store->canView(target)) target = m_channelModel->firstTextChannel();
        m_channel = 0;
        if (target) selectChannel(QString::number(target));
        else {
            m_messageModel->setChannel(0);
            emit selectionChanged();
        }
    } else {
        m_channel = 0;
        m_messageModel->setChannel(0);
        m_memberModel->showPrivate(0);
        const auto dms = m_store->sortedPrivateChannels();
        const Snowflake last = m_lastChannelInGuild.value(0);
        if (last && m_store->channel(last)) selectChannel(QString::number(last));
        else if (!dms.isEmpty()) selectChannel(QString::number(dms.first()));
        emit selectionChanged();
    }
}

void AppController::selectChannel(const QString &id) {
    const Snowflake cid = id.toULongLong();
    const Channel *c = m_store->channel(cid);
    if (!c) return;
    if (c->isVoice()) {
        joinVoice(id);
        return;
    }
    if (c->type == ChannelType::GuildCategory || c->type == ChannelType::GuildForum || c->type == ChannelType::GuildMedia) {
        if (c->type != ChannelType::GuildCategory) emit toast("Forum channels aren't supported yet.");
        return;
    }
    if (c->guildId != m_guild) {
        m_guild = c->guildId;
        m_channelModel->setGuild(m_guild);
    }
    cancelReply();
    cancelEdit();
    m_channel = cid;
    m_lastChannelInGuild[m_guild] = cid;
    m_settings->set("last/channel", QString::number(cid));
    m_channelModel->setSelected(cid);
    m_messageModel->setChannel(cid);
    if (c->guildId) {
        m_memberModel->showGuild(c->guildId);
        m_client->subscribeGuild(c->guildId, cid);
    } else {
        m_memberModel->showPrivate(cid);
    }
    markCurrentRead();
    updateTyping();
    m_typingTimer.start();
    emit selectionChanged();
    emit focusComposer();
}

void AppController::openDmWith(const QString &userId) {
    const Snowflake uid = userId.toULongLong();
    for (auto cid : m_store->privateChannels) {
        const Channel *c = m_store->channel(cid);
        if (c && c->type == ChannelType::DM && c->recipients.contains(uid)) {
            selectGuild({});
            selectChannel(QString::number(cid));
            return;
        }
    }
    m_client->http()->post("/users/@me/channels", { { "recipients", QJsonArray { userId } } }, [this](const HttpResponse &r) {
        if (!r.ok()) {
            emit toast("Couldn't open a DM.");
            return;
        }
        m_store->handleDispatch("CHANNEL_CREATE", r.json.object());
        selectGuild({});
        selectChannel(str(r.json.object(), "id"));
    });
}

void AppController::markCurrentRead() {
    const Channel *c = m_store->channel(m_channel);
    if (!c) return;
    if (m_window && !m_window->isActive()) return;
    m_client->ack(m_channel, c->lastMessageId);
}

void AppController::updateTyping() {
    const auto ids = m_store->typingIn(m_channel);
    QString text;
    QStringList names;
    for (auto u : ids) names.append(m_store->userDisplayName(u, m_guild));
    if (names.size() == 1) text = QString("<b>%1</b> is typing…").arg(names[0].toHtmlEscaped());
    else if (names.size() == 2) text = QString("<b>%1</b> and <b>%2</b> are typing…").arg(names[0].toHtmlEscaped(), names[1].toHtmlEscaped());
    else if (names.size() == 3) text = QString("<b>%1</b>, <b>%2</b> and <b>%3</b> are typing…").arg(names[0].toHtmlEscaped(), names[1].toHtmlEscaped(), names[2].toHtmlEscaped());
    else if (names.size() > 3) text = "Several people are typing…";
    if (text != m_typingText) {
        m_typingText = text;
        emit typingChanged();
    }
}

void AppController::onMessageCreated(const Message &m) {
    if (m.channelId == m_channel) {
        markCurrentRead();
        return;
    }
    if (m.authorId == m_store->self.id || !m_settings->notifications()) return;
    const Channel *c = m_store->channel(m.channelId);
    if (!c) return;
    bool notifyIt = c->isPrivate() && !m_store->isChannelMuted(c->id);
    if (!notifyIt && !m_store->isGuildMuted(c->guildId)) {
        notifyIt = m.mentions.contains(m_store->self.id);
        if (!notifyIt && (m.mentionEveryone || !m.mentionRoles.isEmpty())) {
            const auto roles = m_store->memberRoles(m.guildId, m_store->self.id);
            notifyIt = m.mentionEveryone;
            for (auto r : m.mentionRoles) notifyIt |= roles.contains(r);
        }
    }
    if (!notifyIt) return;
    MarkdownContext ctx;
    ctx.userName = [this, &m](Snowflake id) { return m_store->userDisplayName(id, m.guildId); };
    ctx.channelName = [this](Snowflake id) { auto *ch = m_store->channel(id); return ch ? ch->name : QString(); };
    QString title = m_store->userDisplayName(m.authorId, m.guildId);
    if (!c->isPrivate()) {
        const Guild *g = m_store->guild(c->guildId);
        title += QString(" (#%1%2)").arg(c->name, g ? ", " + g->name : QString());
    }
    QString body = markdownToPlain(m.content, ctx);
    if (body.isEmpty() && !m.attachments.isEmpty()) body = "Sent an attachment";
    notify(title, body.left(240));
}

void AppController::notify(const QString &title, const QString &body) {
    if (m_window && m_window->isActive()) return;
    if (m_tray) m_tray->showMessage(title, body, QSystemTrayIcon::NoIcon, 5000);
    QApplication::alert(nullptr);
}

void AppController::sendMessage(const QString &text, const QStringList &files) {
    if (!m_editingId.isEmpty()) {
        submitEdit(text);
        return;
    }
    const QString trimmed = EmojiIndex::instance().replaceShortcodes(text.trimmed());
    if (trimmed.isEmpty() && files.isEmpty()) return;
    if (!m_channel) return;

    QStringList local;
    for (const auto &f : files) local.append(f.startsWith("file:") ? QUrl(f).toLocalFile() : f);

    Message m;
    m.nonce = QString::number(makeNonce());
    m.channelId = m_channel;
    m.guildId = m_guild;
    m.authorId = m_store->self.id;
    m.content = trimmed;
    m.timestamp = QDateTime::currentDateTimeUtc();
    m.pending = true;
    const Snowflake replyTo = m_replyTo.value("messageId").toString().toULongLong();
    const bool mention = m_replyTo.value("mention", true).toBool();
    if (replyTo) {
        m.replyToId = replyTo;
        m.replyAuthorId = m_replyTo.value("authorId").toString().toULongLong();
        m.replyContent = m_replyTo.value("content").toString();
    }
    for (const auto &f : local) {
        Attachment a;
        a.filename = QFileInfo(f).fileName();
        m.attachments.append(a);
    }
    m_messageModel->addPending(m);
    m_failedFiles.insert(m.nonce, local);

    const QString nonce = m.nonce;
    m_client->sendMessage(m_channel, trimmed, replyTo, mention, local, nonce, [this, nonce](bool ok, const QString &err) {
        if (!ok) {
            m_messageModel->markFailed(nonce);
            emit toast("Message failed to send: " + err);
        } else {
            m_failedFiles.remove(nonce);
        }
    });
    cancelReply();
}

void AppController::retryMessage(const QString &) {
    emit toast("Copy the text and send it again.");
}

void AppController::startReply(const QString &messageId, const QString &authorName) {
    QVariantMap r;
    r["messageId"] = messageId;
    r["author"] = authorName;
    r["mention"] = true;
    const QString raw = m_messageModel->rawContent(messageId);
    r["content"] = raw;
    m_replyTo = r;
    emit replyChanged();
    emit focusComposer();
}

void AppController::setReplyMention(bool mention) {
    if (m_replyTo.isEmpty()) return;
    m_replyTo["mention"] = mention;
    emit replyChanged();
}

void AppController::cancelReply() {
    if (m_replyTo.isEmpty()) return;
    m_replyTo.clear();
    emit replyChanged();
}

void AppController::startEdit(const QString &messageId) {
    const QString id = messageId.isEmpty() ? m_messageModel->lastOwnMessageId() : messageId;
    if (id.isEmpty()) return;
    m_editingId = id;
    emit editingChanged();
    emit editTextRequested(m_messageModel->rawContent(id));
}

void AppController::cancelEdit() {
    if (m_editingId.isEmpty()) return;
    m_editingId.clear();
    emit editingChanged();
}

void AppController::submitEdit(const QString &text) {
    const QString id = m_editingId;
    cancelEdit();
    if (id.isEmpty() || text.trimmed().isEmpty()) return;
    if (text.trimmed() == m_messageModel->rawContent(id)) return;
    m_client->editMessage(m_channel, id.toULongLong(), text.trimmed());
}

void AppController::deleteMessage(const QString &messageId) {
    m_client->deleteMessage(m_channel, messageId.toULongLong());
}

void AppController::toggleReaction(const QString &messageId, const QString &key, bool me) {
    if (me) m_client->removeReaction(m_channel, messageId.toULongLong(), key);
    else m_client->addReaction(m_channel, messageId.toULongLong(), key);
}

void AppController::userTyping() {
    if (m_channel && m_editingId.isEmpty()) m_client->sendTyping(m_channel);
}

void AppController::handleLink(const QString &url) {
    if (url.startsWith("kestrel://channel/")) {
        const QString id = url.mid(18);
        if (m_store->channel(id.toULongLong())) selectChannel(id);
        return;
    }
    if (url.startsWith("kestrel://user/")) return; // handled by QML (profile popup)
    const QUrl u(url);
    // discord.com/channels/<guild|@me>/<channel>[/<message>] links open in-app
    if ((u.host() == "discord.com" || u.host().endsWith(".discord.com")) && u.path().startsWith("/channels/")) {
        const auto parts = u.path().split('/', Qt::SkipEmptyParts);
        if (parts.size() >= 3 && m_store->channel(parts[2].toULongLong())) {
            selectChannel(parts[2]);
            if (parts.size() >= 4) m_messageModel->jumpTo(parts[3]);
            return;
        }
    }
    QDesktopServices::openUrl(u);
}

void AppController::copyText(const QString &text) {
    QGuiApplication::clipboard()->setText(text);
}

void AppController::setStatus(const QString &status) {
    m_client->setStatus(status);
}

QVariantMap AppController::userProfile(const QString &userId) {
    const Snowflake uid = userId.toULongLong();
    QVariantMap m;
    const User *u = m_store->user(uid);
    if (!u) return m;
    m["id"] = userId;
    m["name"] = m_store->userDisplayName(uid, m_guild);
    m["username"] = u->username;
    m["avatar"] = urls::avatar(*u, 80);
    m["status"] = uid == m_store->self.id ? m_store->selfStatus : m_store->presence(uid);
    m["bot"] = u->bot;
    m["isSelf"] = uid == m_store->self.id;
    QVariantList roles;
    if (const Guild *g = m_store->guild(m_guild)) {
        auto ids = m_store->memberRoles(m_guild, uid);
        std::sort(ids.begin(), ids.end(), [g](Snowflake a, Snowflake b) { return g->roles.value(a).position > g->roles.value(b).position; });
        for (auto r : ids) {
            auto it = g->roles.find(r);
            if (it == g->roles.end()) continue;
            roles.append(QVariantMap { { "name", it->name }, { "color", it->color ? QColor::fromRgb(it->color).name() : QString("#b5bac1") } });
        }
    }
    m["roles"] = roles;
    return m;
}

void AppController::requestMemberList(int visibleEnd) {
    if (m_guild && m_channel) m_client->subscribeGuild(m_guild, m_channel, qBound(99, ((visibleEnd / 100) + 1) * 100 - 1, 299));
}

// ------------------------------------------------------------------ voice

void AppController::onVoiceStateChanged(const VoiceState &vs, Snowflake previousChannel) {
    const Channel *now = m_store->channel(vs.channelId);
    if ((now && now->guildId == m_guild) || (previousChannel && m_store->channel(previousChannel) && m_store->channel(previousChannel)->guildId == m_guild))
        m_channelModel->refreshVoice();
    if (now && now->guildId == m_guild && vs.channelId && previousChannel != vs.channelId) {
        // a newly occupied voice channel inside a collapsed category needs to appear
        m_channelModel->rebuild();
    }
    if (vs.userId != m_store->self.id) return;
#ifdef KESTREL_VOICE
    // Voice states from our other devices (e.g. a call on the phone) aren't ours to handle.
    if (!vs.sessionId.isEmpty() && vs.sessionId != m_client->gateway()->sessionId()) {
        if (m_voiceChannel && m_voice->state() != "connected") {
            // another device took over the call we were trying to start
            m_voice->disconnectVoice();
            m_voicePoll.stop();
            m_voiceChannel = m_voiceGuild = 0;
            emit toast("You joined voice on another device.");
        }
        emit voiceChanged();
        return;
    }
    if (vs.channelId == 0) {
        if (m_voiceChannel) {
            m_voice->disconnectVoice();
            m_voicePoll.stop();
            m_voiceChannel = m_voiceGuild = 0;
            m_channelModel->setSpeaking({});
        }
    } else {
        m_voiceChannel = vs.channelId;
        m_voiceGuild = vs.guildId;
        m_voice->setSession(vs.userId, vs.sessionId);
    }
#endif
    emit voiceChanged();
}

QString AppController::voiceState() const {
#ifdef KESTREL_VOICE
    if (!m_voiceChannel) return "disconnected";
    return m_voice->state() == "disconnected" ? QString("connecting") : m_voice->state();
#else
    return "disconnected";
#endif
}

QString AppController::voiceChannelName() const {
    const Channel *c = m_store->channel(m_voiceChannel);
    if (!c) return {};
    const Guild *g = m_store->guild(c->guildId);
    return g ? QString("%1 / %2").arg(c->name, g->name) : c->name;
}

bool AppController::voiceEncrypted() const {
#ifdef KESTREL_VOICE
    return m_voice->encrypted();
#else
    return false;
#endif
}

int AppController::voicePing() const {
#ifdef KESTREL_VOICE
    return m_voice->ping();
#else
    return 0;
#endif
}

double AppController::inputLevel() const {
#ifdef KESTREL_VOICE
    return m_audio->inputLevelDb();
#else
    return -100;
#endif
}

bool AppController::voiceAvailable() const {
#ifdef KESTREL_VOICE
    return true;
#else
    return false;
#endif
}

void AppController::joinVoice(const QString &channelId) {
#ifdef KESTREL_VOICE
    const Channel *c = m_store->channel(channelId.toULongLong());
    if (!c) return;
    if (!(m_store->permissions(c->id) & perm::Connect)) {
        emit toast("You don't have permission to join that channel.");
        return;
    }
    if (m_voiceChannel == c->id) return;
    stopMicTest();
    m_audio->setMuted(m_selfMuted);
    m_audio->setDeafened(m_selfDeafened);
    m_voiceChannel = c->id;
    m_voiceGuild = c->guildId;
    m_client->updateVoiceState(c->guildId, c->id, m_selfMuted, m_selfDeafened);
    emit voiceChanged();
#else
    Q_UNUSED(channelId)
    emit toast("This build was compiled without voice support.");
#endif
}

void AppController::leaveVoice() {
#ifdef KESTREL_VOICE
    if (!m_voiceChannel) return;
    m_client->updateVoiceState(m_voiceGuild, 0, m_selfMuted, m_selfDeafened);
    m_voice->disconnectVoice();
    m_voicePoll.stop();
    m_voiceChannel = m_voiceGuild = 0;
    m_channelModel->setSpeaking({});
    emit voiceChanged();
#endif
}

void AppController::toggleMute() {
    m_selfMuted = !m_selfMuted;
    if (!m_selfMuted && m_selfDeafened) m_selfDeafened = false;
#ifdef KESTREL_VOICE
    m_audio->setMuted(m_selfMuted);
    m_audio->setDeafened(m_selfDeafened);
    if (m_voiceChannel) m_client->updateVoiceState(m_voiceGuild, m_voiceChannel, m_selfMuted, m_selfDeafened);
#endif
    emit voiceChanged();
}

void AppController::toggleDeafen() {
    m_selfDeafened = !m_selfDeafened;
    m_selfMuted = m_selfDeafened ? true : m_selfMuted;
#ifdef KESTREL_VOICE
    m_audio->setMuted(m_selfMuted);
    m_audio->setDeafened(m_selfDeafened);
    if (m_voiceChannel) m_client->updateVoiceState(m_voiceGuild, m_voiceChannel, m_selfMuted, m_selfDeafened);
#endif
    emit voiceChanged();
}

void AppController::setUserVolume(const QString &userId, double volume) {
    m_settings->setUserVolume(userId, volume);
#ifdef KESTREL_VOICE
    m_voice->setUserVolume(userId.toULongLong(), static_cast<float>(volume));
#endif
}

void AppController::refreshDevices() {
#ifdef KESTREL_VOICE
    m_inputDevices = { "Default" };
    m_outputDevices = { "Default" };
    for (const auto &d : m_audio->inputDevices()) m_inputDevices.append(QString::fromStdString(d.name));
    for (const auto &d : m_audio->outputDevices()) m_outputDevices.append(QString::fromStdString(d.name));
    emit devicesChanged();
#endif
}

void AppController::startMicTest() {
#ifdef KESTREL_VOICE
    m_micTest = true;
    m_audio->setInputGain(static_cast<float>(m_settings->inputVolume()));
    m_audio->startMonitor(m_settings->inputDevice().toStdString());
    m_voicePoll.start();
#endif
}

void AppController::stopMicTest() {
#ifdef KESTREL_VOICE
    if (!m_micTest) return;
    m_micTest = false;
    m_audio->stopMonitor();
    if (!m_voiceChannel) m_voicePoll.stop();
#endif
}

void AppController::applyVoiceSettings() {
#ifdef KESTREL_VOICE
    m_audio->setPushToTalk(m_settings->pushToTalk(), m_settings->pushToTalkKey());
    m_audio->setVadThreshold(static_cast<float>(m_settings->vadThreshold()));
    m_audio->setInputGain(static_cast<float>(m_settings->inputVolume()));
    m_audio->setOutputGain(static_cast<float>(m_settings->outputVolume()));
    if (m_voiceChannel && m_voice->state() == "connected") {
        // device changes need the streams reopened
        m_audio->start(m_settings->inputDevice().toStdString(), m_settings->outputDevice().toStdString());
    }
#endif
}

QString AppController::keyName(int qtKey, int nativeVk, const QString &text) const {
    Q_UNUSED(nativeVk)
    const QString seq = QKeySequence(qtKey).toString(QKeySequence::NativeText);
    if (!seq.isEmpty()) return seq;
    return text.toUpper();
}

static QString customEmojiUrl(const CustomEmoji &e, int px) {
    return urls::provider(QString("https://cdn.discordapp.com/emojis/%1.png?size=%2").arg(e.id).arg(urls::cdnSize(px * 2)), px, px, false);
}

QVariantList AppController::emojiRows(const QString &filter, int columns) {
    QVariantList rows;
    QVariantList current;
    auto flush = [&]() {
        if (!current.isEmpty()) rows.append(QVariantMap { { "items", current } });
        current.clear();
    };
    auto add = [&](const QVariantMap &item) {
        current.append(item);
        if (current.size() >= columns) flush();
    };
    const QString q = filter.trimmed().toLower();

    // this server's custom emoji first
    if (const Guild *g = m_store->guild(m_guild); g && !g->emojis.isEmpty()) {
        bool header = false;
        for (const auto &e : g->emojis) {
            if (!q.isEmpty() && !e.name.toLower().contains(q)) continue;
            if (!header) {
                rows.append(QVariantMap { { "header", g->name } });
                header = true;
            }
            add({ { "image", customEmojiUrl(e, 32) }, { "name", e.name },
                  { "value", QString("<%1:%2:%3>").arg(e.animated ? "a" : "").arg(e.name).arg(e.id) },
                  { "reaction", QString("%1:%2").arg(e.name).arg(e.id) } });
        }
        flush();
    }
    const auto &idx = EmojiIndex::instance();
    if (!q.isEmpty()) {
        const auto found = idx.search(q, 120);
        if (!found.isEmpty()) rows.append(QVariantMap { { "header", "Results" } });
        for (const auto *e : found) add({ { "emoji", e->chars }, { "name", e->names.value(0) }, { "value", e->chars }, { "reaction", e->chars } });
        flush();
        return rows;
    }
    for (const auto &g : idx.groups()) {
        rows.append(QVariantMap { { "header", g.name } });
        for (const auto &e : g.emojis) add({ { "emoji", e.chars }, { "name", e.names.value(0) }, { "value", e.chars }, { "reaction", e.chars } });
        flush();
    }
    return rows;
}

QVariantList AppController::autocomplete(const QString &trigger, const QString &query) {
    QVariantList out;
    const QString q = query.toLower();
    const int limit = 8;
    if (trigger == "@") {
        QSet<Snowflake> seen;
        auto consider = [&](Snowflake uid) {
            if (out.size() >= limit || seen.contains(uid)) return;
            const User *u = m_store->user(uid);
            if (!u) return;
            const QString name = m_store->userDisplayName(uid, m_guild);
            if (!q.isEmpty() && !name.toLower().startsWith(q) && !u->username.toLower().startsWith(q)) return;
            seen.insert(uid);
            out.append(QVariantMap { { "label", name }, { "sub", u->username }, { "image", urls::avatar(*u, 24) },
                                     { "display", "@" + name }, { "value", QString("<@%1>").arg(uid) } });
        };
        if (const Channel *c = m_store->channel(m_channel); c && c->isPrivate()) {
            for (auto r : c->recipients) consider(r);
        }
        if (m_guild) {
            for (auto it = m_store->members.begin(); it != m_store->members.end() && out.size() < limit; ++it)
                if (it.key().first == m_guild) consider(it.key().second);
            if (const Guild *g = m_store->guild(m_guild)) {
                for (const auto &r : g->roles) {
                    if (out.size() >= limit) break;
                    if (r.id == g->id || !r.name.toLower().startsWith(q)) continue;
                    out.append(QVariantMap { { "label", "@" + r.name }, { "sub", "Role" }, { "color", r.color ? QColor::fromRgb(r.color).name() : QString() },
                                             { "display", "@" + r.name }, { "value", QString("<@&%1>").arg(r.id) } });
                }
            }
            for (const QString &special : { QStringLiteral("everyone"), QStringLiteral("here") })
                if (special.startsWith(q) && out.size() < limit)
                    out.append(QVariantMap { { "label", "@" + special }, { "sub", "Notify everyone" }, { "display", "@" + special }, { "value", "@" + special } });
        }
    } else if (trigger == "#") {
        if (const Guild *g = m_store->guild(m_guild)) {
            for (auto cid : g->channels) {
                const Channel *c = m_store->channel(cid);
                if (!c || c->type == ChannelType::GuildCategory || !m_store->canView(cid)) continue;
                if (!c->name.toLower().contains(q)) continue;
                const Channel *parent = m_store->channel(c->parentId);
                out.append(QVariantMap { { "label", c->name }, { "sub", parent ? parent->name : QString() }, { "icon", c->isVoice() ? "speaker" : "hash" },
                                         { "display", "#" + c->name }, { "value", QString("<#%1>").arg(cid) } });
                if (out.size() >= limit) break;
            }
        }
    } else if (trigger == ":") {
        if (const Guild *g = m_store->guild(m_guild)) {
            for (const auto &e : g->emojis) {
                if (!e.name.toLower().contains(q)) continue;
                out.append(QVariantMap { { "label", ":" + e.name + ":" }, { "image", customEmojiUrl(e, 24) },
                                         { "display", ":" + e.name + ":" }, { "value", QString("<%1:%2:%3>").arg(e.animated ? "a" : "").arg(e.name).arg(e.id) } });
                if (out.size() >= limit) break;
            }
        }
        for (const auto *e : EmojiIndex::instance().search(q, limit - out.size()))
            out.append(QVariantMap { { "label", ":" + e->names.value(0) + ":" }, { "emoji", e->chars }, { "display", e->chars }, { "value", e->chars } });
    }
    return out;
}

QVariantList AppController::quickSwitch(const QString &query) {
    QVariantList out;
    const QString q = query.trimmed().toLower();
    auto score = [&](const QString &name) -> int {
        const QString n = name.toLower();
        if (q.isEmpty()) return 1;
        if (n.startsWith(q)) return 3;
        if (n.contains(q)) return 2;
        return 0;
    };
    struct Hit { int score; QVariantMap item; };
    QVector<Hit> hits;
    for (auto cid : m_store->sortedPrivateChannels()) {
        const Channel *c = m_store->channel(cid);
        if (!c) continue;
        const QString name = m_store->privateChannelName(*c);
        if (int sc = score(name)) {
            Snowflake other = 0;
            for (auto r : c->recipients) if (r != m_store->self.id) { other = r; break; }
            const User *u = m_store->user(other);
            hits.append({ sc + 1, { { "kind", "dm" }, { "id", QString::number(cid) }, { "name", name }, { "sub", "Direct Message" },
                                    { "image", c->type == ChannelType::DM && u ? urls::avatar(*u, 24) : QString() }, { "unread", m_store->isUnread(cid) } } });
        }
    }
    for (auto gid : m_store->guildOrder) {
        const Guild *g = m_store->guild(gid);
        if (!g) continue;
        for (auto cid : g->channels) {
            const Channel *c = m_store->channel(cid);
            if (!c || c->type == ChannelType::GuildCategory || !m_store->canView(cid)) continue;
            if (int sc = score(c->name))
                hits.append({ sc, { { "kind", c->isVoice() ? "voice" : "channel" }, { "id", QString::number(cid) }, { "name", c->name }, { "sub", g->name },
                                    { "unread", m_store->isUnread(cid) && !m_store->isChannelMuted(cid) } } });
        }
        if (int sc = score(g->name)) hits.append({ sc, { { "kind", "guild" }, { "id", QString::number(gid) }, { "name", g->name }, { "sub", "Server" }, { "image", urls::guildIcon(*g, 24) } } });
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        if (a.score != b.score) return a.score > b.score;
        return a.item.value("unread").toBool() && !b.item.value("unread").toBool();
    });
    for (int i = 0; i < hits.size() && i < 12; i++) out.append(hits[i].item);
    return out;
}

void AppController::screenshot(const QString &path) {
    if (m_window) m_window->grabWindow().save(path);
}

QString AppController::memoryUsage() const {
#ifdef _WIN32
    return {};
#else
    QFile f("/proc/self/status");
    if (!f.open(QIODevice::ReadOnly)) return {};
    for (const auto &line : QString::fromUtf8(f.readAll()).split('\n'))
        if (line.startsWith("VmRSS")) return line.simplified();
    return {};
#endif
}

} // namespace kestrel
