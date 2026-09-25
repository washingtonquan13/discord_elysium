#include "Store.h"

#include "Json.h"
#include "Permissions.h"

#include <QDateTime>
#include <QJsonArray>
#include <algorithm>

namespace kestrel {

using namespace json;

namespace {

QDateTime parseTime(const QString &s) {
    if (s.isEmpty()) return {};
    return QDateTime::fromString(s, Qt::ISODateWithMs);
}

// CLIENT_STATE_V2 style guilds nest their fields in "properties"; flatten.
QJsonObject flattenGuild(const QJsonObject &o) {
    if (!has(o, "properties")) return o;
    QJsonObject g = obj(o, "properties");
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (it.key() != "properties") g.insert(it.key(), it.value());
    }
    return g;
}

EmbedMedia parseMedia(const QJsonObject &o) {
    EmbedMedia m;
    m.url = str(o, "url");
    m.proxyUrl = str(o, "proxy_url");
    m.width = i32(o, "width");
    m.height = i32(o, "height");
    return m;
}

QJsonArray entries(const QJsonValue &v) {
    if (v.isArray()) return v.toArray();
    if (v.isObject()) return v.toObject().value("entries").toArray();
    return {};
}

} // namespace

Store::Store(QObject *parent)
    : QObject(parent) {}

void Store::clear() {
    self = {};
    users.clear();
    guilds.clear();
    guildOrder.clear();
    folders.clear();
    channels.clear();
    privateChannels.clear();
    readStates.clear();
    mutedGuilds.clear();
    mutedChannels.clear();
    voiceStates.clear();
    presences.clear();
    typing.clear();
    members.clear();
    relationshipsFriends.clear();
    ready = false;
}

User &Store::upsertUser(const QJsonObject &o) {
    const Snowflake uid = id(o);
    User &u = users[uid];
    u.id = uid;
    if (has(o, "username")) u.username = str(o, "username");
    if (has(o, "global_name")) u.globalName = str(o, "global_name");
    if (has(o, "discriminator")) u.discriminator = str(o, "discriminator");
    if (has(o, "avatar")) u.avatar = str(o, "avatar");
    if (has(o, "bot")) u.bot = boolean(o, "bot");
    if (uid == self.id && uid != 0) {
        const auto keep = self;
        self = u;
        if (self.username.isEmpty()) self.username = keep.username;
    }
    return u;
}

Channel Store::parseChannel(const QJsonObject &o, Snowflake guildId) {
    Channel c;
    c.id = id(o);
    c.guildId = has(o, "guild_id") ? id(o, "guild_id") : guildId;
    c.parentId = id(o, "parent_id");
    c.lastMessageId = id(o, "last_message_id");
    c.ownerId = id(o, "owner_id");
    c.type = static_cast<ChannelType>(i32(o, "type"));
    c.name = str(o, "name");
    c.topic = str(o, "topic");
    c.icon = str(o, "icon");
    c.position = i32(o, "position");
    c.userLimit = i32(o, "user_limit");
    c.nsfw = boolean(o, "nsfw");
    for (const auto &v : arr(o, "permission_overwrites")) {
        const auto ow = v.toObject();
        Overwrite w;
        w.id = id(ow);
        w.type = ow.value("type").isString() ? (str(ow, "type") == "member" ? 1 : 0) : i32(ow, "type");
        w.allow = bits(ow, "allow");
        w.deny = bits(ow, "deny");
        c.overwrites.append(w);
    }
    for (const auto &v : arr(o, "recipient_ids")) c.recipients.append(toId(v));
    for (const auto &v : arr(o, "recipients")) {
        const auto &u = upsertUser(v.toObject());
        c.recipients.append(u.id);
    }
    return c;
}

Guild Store::parseGuild(const QJsonObject &raw) {
    const QJsonObject o = flattenGuild(raw);
    Guild g;
    g.id = id(o);
    g.unavailable = boolean(o, "unavailable");
    g.name = str(o, "name");
    g.icon = str(o, "icon");
    g.ownerId = id(o, "owner_id");
    g.memberCount = i32(o, "member_count");
    g.large = boolean(o, "large");
    for (const auto &v : arr(o, "roles")) {
        const auto r = v.toObject();
        Role role;
        role.id = id(r);
        role.name = str(r, "name");
        role.color = static_cast<quint32>(i64(r, "color"));
        role.position = i32(r, "position");
        role.permissions = bits(r, "permissions");
        role.hoist = boolean(r, "hoist");
        g.roles.insert(role.id, role);
    }
    for (const auto &v : arr(o, "emojis")) {
        const auto e = v.toObject();
        g.emojis.append({ id(e), str(e, "name"), boolean(e, "animated") });
    }
    return g;
}

void Store::ingestGuild(const QJsonObject &raw) {
    const QJsonObject o = flattenGuild(raw);
    Guild g = parseGuild(o);
    if (g.id == 0) return;
    if (auto it = guilds.find(g.id); it != guilds.end()) {
        g.self = it->self; // keep our membership info
        if (g.unavailable) {
            it->unavailable = true;
            return;
        }
    }
    for (const auto &v : arr(o, "channels")) {
        Channel c = parseChannel(v.toObject(), g.id);
        g.channels.append(c.id);
        channels.insert(c.id, c);
    }
    for (const auto &v : arr(o, "threads")) {
        Channel c = parseChannel(v.toObject(), g.id);
        channels.insert(c.id, c);
    }
    for (const auto &v : arr(o, "voice_states")) setVoiceState(v.toObject(), g.id);
    for (const auto &v : arr(o, "members")) storeMember(g.id, v.toObject());
    for (const auto &v : arr(o, "presences")) setPresence(v.toObject());
    if (const MemberInfo *mi = member(g.id, self.id)) g.self = *mi;
    guilds.insert(g.id, g);
    if (!guildOrder.contains(g.id)) guildOrder.append(g.id);
}

void Store::storeMember(Snowflake guildId, const QJsonObject &m, Snowflake userId) {
    if (has(m, "user")) userId = upsertUser(obj(m, "user")).id;
    if (userId == 0) userId = id(m, "user_id");
    if (userId == 0 || guildId == 0) return;
    MemberInfo info;
    info.nick = str(m, "nick");
    info.avatar = str(m, "avatar");
    for (const auto &r : arr(m, "roles")) info.roles.append(toId(r));
    members.insert({ guildId, userId }, info);
    if (userId == self.id) {
        if (auto it = guilds.find(guildId); it != guilds.end()) it->self = info;
    }
}

void Store::setVoiceState(const QJsonObject &o, Snowflake guildId) {
    VoiceState vs;
    vs.userId = id(o, "user_id");
    vs.guildId = has(o, "guild_id") ? id(o, "guild_id") : guildId;
    vs.channelId = id(o, "channel_id");
    vs.sessionId = str(o, "session_id");
    vs.selfMute = boolean(o, "self_mute");
    vs.selfDeaf = boolean(o, "self_deaf");
    vs.mute = boolean(o, "mute");
    vs.deaf = boolean(o, "deaf");
    vs.selfStream = boolean(o, "self_stream");
    vs.selfVideo = boolean(o, "self_video");
    if (has(o, "member")) storeMember(vs.guildId, obj(o, "member"), vs.userId);
    if (vs.userId == 0) return;

    const Snowflake prev = voiceStates.contains(vs.userId) ? voiceStates[vs.userId].channelId : 0;
    if (vs.channelId == 0) voiceStates.remove(vs.userId);
    else voiceStates.insert(vs.userId, vs);
    if (ready) emit voiceStateChanged(vs, prev);
}

void Store::setPresence(const QJsonObject &o) {
    Snowflake uid = id(o, "user_id");
    if (uid == 0 && has(o, "user")) uid = id(obj(o, "user"));
    if (uid == 0) return;
    presences.insert(uid, str(o, "status", "offline"));
    if (ready) emit presenceChanged(uid);
}

void Store::rebuildGuildOrder() {
    if (folders.isEmpty()) return;
    QVector<Snowflake> order;
    for (const auto &f : folders)
        for (auto gid : f.guildIds)
            if (guilds.contains(gid) && !order.contains(gid)) order.append(gid);
    // servers missing from the saved layout go on top (like newly joined ones)
    QVector<Snowflake> missing;
    for (auto gid : guildOrder)
        if (!order.contains(gid)) missing.append(gid);
    guildOrder = missing + order;
}

void Store::handleReady(const QJsonObject &d) {
    clear();
    const auto u = obj(d, "user");
    self.id = id(u);
    upsertUser(u);
    self = users[self.id];

    for (const auto &v : arr(d, "users")) upsertUser(v.toObject());

    const auto gs = arr(d, "guilds");
    const auto merged = arr(d, "merged_members");
    for (int i = 0; i < gs.size(); i++) {
        ingestGuild(gs[i].toObject());
        const Snowflake gid = id(gs[i].toObject());
        if (i < merged.size()) {
            for (const auto &mv : merged[i].toArray()) {
                const auto m = mv.toObject();
                if (id(m, "user_id") == self.id || id(obj(m, "user")) == self.id) storeMember(gid, m, self.id);
            }
        }
    }

    for (const auto &v : arr(d, "private_channels")) {
        Channel c = parseChannel(v.toObject(), 0);
        channels.insert(c.id, c);
        privateChannels.append(c.id);
    }

    for (const auto &v : entries(d.value("read_state"))) {
        const auto r = v.toObject();
        if (i32(r, "read_state_type", 0) != 0) continue;
        ReadState rs;
        rs.lastMessageId = id(r, "last_message_id");
        rs.mentionCount = i32(r, "mention_count");
        readStates.insert(id(r), rs);
    }

    for (const auto &v : entries(d.value("user_guild_settings"))) {
        const auto s = v.toObject();
        const Snowflake gid = id(s, "guild_id");
        if (boolean(s, "muted") && gid) mutedGuilds.insert(gid);
        for (const auto &ov : arr(s, "channel_overrides")) {
            const auto c = ov.toObject();
            if (boolean(c, "muted")) mutedChannels.insert(id(c, "channel_id"));
        }
    }

    for (const auto &v : arr(d, "relationships")) {
        const auto r = v.toObject();
        if (i32(r, "type") == 1) relationshipsFriends.insert(id(r));
        if (has(r, "user")) upsertUser(obj(r, "user"));
    }

    // server order/folders
    if (has(d, "user_settings_proto")) folders = decodeGuildFolders(str(d, "user_settings_proto").toLatin1());
    if (folders.isEmpty()) {
        for (const auto &fv : arr(obj(d, "user_settings"), "guild_folders")) {
            const auto f = fv.toObject();
            GuildFolder gf;
            gf.id = i64(f, "id");
            gf.name = str(f, "name");
            gf.color = isNull(f, "color") ? -1 : i64(f, "color");
            for (const auto &g : arr(f, "guild_ids")) gf.guildIds.append(toId(g));
            folders.append(gf);
        }
    }
    rebuildGuildOrder();

    for (const auto &v : arr(d, "sessions")) {
        const auto s = v.toObject();
        if (str(s, "session_id") == "all" || selfStatus.isEmpty()) selfStatus = str(s, "status", "online");
    }

    const auto rotated = str(d, "auth_token");
    if (!rotated.isEmpty()) {
        authToken = rotated;
        emit authTokenRotated(rotated);
    }

    ready = true;
    emit readyReceived();
}

void Store::handleReadySupplemental(const QJsonObject &d) {
    const auto mp = obj(d, "merged_presences");
    for (const auto &list : arr(mp, "guilds"))
        for (const auto &p : list.toArray()) setPresence(p.toObject());
    for (const auto &p : arr(mp, "friends")) setPresence(p.toObject());
    for (const auto &gv : arr(d, "guilds")) {
        const auto g = gv.toObject();
        const Snowflake gid = id(g);
        for (const auto &v : arr(g, "voice_states")) setVoiceState(v.toObject(), gid);
    }
    const auto gs = arr(d, "guilds");
    const auto mm = arr(d, "merged_members");
    for (int i = 0; i < gs.size() && i < mm.size(); i++) {
        const Snowflake gid = id(gs[i].toObject());
        for (const auto &m : mm[i].toArray()) storeMember(gid, m.toObject());
    }
    emit guildsChanged();
    for (auto gid : guildOrder) emit channelsChanged(gid);
}

void Store::applyMessageUpdate(Message &m, const QJsonObject &o) {
    if (has(o, "content")) m.content = str(o, "content");
    if (has(o, "edited_timestamp")) m.edited = parseTime(str(o, "edited_timestamp"));
    if (has(o, "pinned")) m.pinned = boolean(o, "pinned");
    if (has(o, "flags")) m.flags = i32(o, "flags");
    if (has(o, "embeds") || has(o, "attachments") || has(o, "mentions")) {
        const Message fresh = parseMessage(o);
        if (has(o, "embeds")) m.embeds = fresh.embeds;
        if (has(o, "attachments")) m.attachments = fresh.attachments;
        if (has(o, "mentions")) {
            m.mentions = fresh.mentions;
            m.mentionEveryone = fresh.mentionEveryone;
            m.mentionRoles = fresh.mentionRoles;
        }
    }
}

Message Store::parseMessage(const QJsonObject &o) {
    Message m;
    m.id = id(o);
    m.channelId = id(o, "channel_id");
    m.guildId = id(o, "guild_id");
    if (m.guildId == 0) {
        if (auto *c = channel(m.channelId)) m.guildId = c->guildId;
    }
    if (has(o, "author")) m.authorId = upsertUser(obj(o, "author")).id;
    if (has(o, "member") && m.guildId) storeMember(m.guildId, obj(o, "member"), m.authorId);
    m.webhookId = id(o, "webhook_id");
    m.type = i32(o, "type");
    m.flags = i32(o, "flags");
    m.content = str(o, "content");
    m.timestamp = parseTime(str(o, "timestamp"));
    if (!m.timestamp.isValid() && m.id) m.timestamp = snowflakeTime(m.id);
    m.edited = parseTime(str(o, "edited_timestamp"));
    m.pinned = boolean(o, "pinned");
    m.mentionEveryone = boolean(o, "mention_everyone");
    m.nonce = str(o, "nonce");

    for (const auto &v : arr(o, "mentions")) m.mentions.append(upsertUser(v.toObject()).id);
    for (const auto &v : arr(o, "mention_roles")) m.mentionRoles.append(toId(v));

    for (const auto &v : arr(o, "attachments")) {
        const auto a = v.toObject();
        Attachment at;
        at.id = id(a);
        at.filename = str(a, "filename");
        at.url = str(a, "url");
        at.proxyUrl = str(a, "proxy_url");
        at.contentType = str(a, "content_type");
        at.size = i64(a, "size");
        at.width = i32(a, "width");
        at.height = i32(a, "height");
        at.spoiler = at.filename.startsWith("SPOILER_") || (i32(a, "flags") & 8);
        m.attachments.append(at);
    }

    for (const auto &v : arr(o, "embeds")) {
        const auto e = v.toObject();
        Embed em;
        em.type = str(e, "type");
        em.title = str(e, "title");
        em.description = str(e, "description");
        em.url = str(e, "url");
        em.color = has(e, "color") ? i32(e, "color") : -1;
        em.authorName = str(obj(e, "author"), "name");
        em.authorIcon = str(obj(e, "author"), "proxy_icon_url", str(obj(e, "author"), "icon_url"));
        em.providerName = str(obj(e, "provider"), "name");
        em.footer = str(obj(e, "footer"), "text");
        em.image = parseMedia(obj(e, "image"));
        em.thumbnail = parseMedia(obj(e, "thumbnail"));
        if (em.image.url.isEmpty() && has(e, "video") && !em.thumbnail.url.isEmpty() && em.type != "video") {
            // gifv (tenor/giphy): the thumbnail is a still of the video
        }
        for (const auto &fv : arr(e, "fields")) {
            const auto f = fv.toObject();
            em.fields.append({ str(f, "name"), str(f, "value"), boolean(f, "inline") });
        }
        m.embeds.append(em);
    }

    for (const auto &v : arr(o, "reactions")) {
        const auto r = v.toObject();
        const auto e = obj(r, "emoji");
        Reaction re;
        re.emojiId = id(e);
        re.emojiName = str(e, "name");
        re.animated = boolean(e, "animated");
        re.count = i32(r, "count", 1);
        re.me = boolean(r, "me") || boolean(r, "me_burst");
        m.reactions.append(re);
    }

    for (const auto &v : arr(o, "sticker_items")) m.stickers.append(str(v.toObject(), "name"));

    if (has(o, "message_reference")) {
        const auto ref = obj(o, "message_reference");
        m.replyToId = id(ref, "message_id");
        if (o.value("referenced_message").isObject()) {
            const auto rm = obj(o, "referenced_message");
            if (has(rm, "author")) m.replyAuthorId = upsertUser(obj(rm, "author")).id;
            m.replyContent = str(rm, "content");
            if (m.replyContent.isEmpty() && !arr(rm, "attachments").isEmpty()) m.replyContent = "Click to see attachment";
            if (m.replyContent.isEmpty() && !arr(rm, "embeds").isEmpty()) m.replyContent = "Click to see embed";
        } else if (m.type == 19) {
            m.replyDeleted = true;
        }
    }
    return m;
}

const User *Store::user(Snowflake id) const {
    auto it = users.find(id);
    return it == users.end() ? nullptr : &*it;
}

const Guild *Store::guild(Snowflake id) const {
    auto it = guilds.find(id);
    return it == guilds.end() ? nullptr : &*it;
}

const Channel *Store::channel(Snowflake id) const {
    auto it = channels.find(id);
    return it == channels.end() ? nullptr : &*it;
}

const MemberInfo *Store::member(Snowflake guildId, Snowflake userId) const {
    auto it = members.find({ guildId, userId });
    return it == members.end() ? nullptr : &*it;
}

QVector<Snowflake> Store::memberRoles(Snowflake guildId, Snowflake userId) const {
    if (userId == self.id) {
        if (auto *g = guild(guildId)) return g->self.roles;
    }
    if (auto *m = member(guildId, userId)) return m->roles;
    return {};
}

QString Store::userDisplayName(Snowflake userId, Snowflake guildId) const {
    if (guildId) {
        if (auto *m = member(guildId, userId); m && !m->nick.isEmpty()) return m->nick;
    }
    if (auto *u = user(userId)) return u->displayName();
    return {};
}

quint32 Store::userColor(Snowflake userId, Snowflake guildId) const {
    auto *g = guild(guildId);
    if (!g) return 0;
    const auto roles = memberRoles(guildId, userId);
    int best = -1;
    quint32 color = 0;
    for (auto r : roles) {
        auto it = g->roles.find(r);
        if (it == g->roles.end() || it->color == 0) continue;
        if (it->position > best) {
            best = it->position;
            color = it->color;
        }
    }
    return color;
}

quint64 Store::permissions(Snowflake channelId) const {
    auto *c = channel(channelId);
    if (!c) return 0;
    if (c->isPrivate()) return perm::All;
    auto *g = guild(c->guildId);
    if (!g) return 0;
    // threads inherit their parent's permissions
    const Channel *base = c;
    if ((c->type == ChannelType::PublicThread || c->type == ChannelType::PrivateThread || c->type == ChannelType::NewsThread) && c->parentId) {
        if (auto *p = channel(c->parentId)) base = p;
    }
    return perm::channelPermissions(*g, *base, self.id, g->self.roles);
}

bool Store::canView(Snowflake channelId) const {
    return permissions(channelId) & perm::ViewChannel;
}

bool Store::isChannelMuted(Snowflake channelId) const {
    if (mutedChannels.contains(channelId)) return true;
    if (auto *c = channel(channelId); c && c->parentId && mutedChannels.contains(c->parentId)) return true;
    return false;
}

bool Store::isGuildMuted(Snowflake guildId) const {
    return mutedGuilds.contains(guildId);
}

bool Store::isUnread(Snowflake channelId) const {
    auto *c = channel(channelId);
    if (!c || c->lastMessageId == 0) return false;
    auto it = readStates.find(channelId);
    if (it == readStates.end()) return false;
    return c->lastMessageId > it->lastMessageId;
}

int Store::mentionCount(Snowflake channelId) const {
    return readStates.value(channelId).mentionCount;
}

bool Store::guildUnread(Snowflake guildId) const {
    auto *g = guild(guildId);
    if (!g || isGuildMuted(guildId)) return false;
    for (auto cid : g->channels) {
        auto *c = channel(cid);
        if (!c || !c->isText() || isChannelMuted(cid)) continue;
        if (isUnread(cid) && canView(cid)) return true;
    }
    return false;
}

int Store::guildMentions(Snowflake guildId) const {
    auto *g = guild(guildId);
    if (!g) return 0;
    int total = 0;
    for (auto cid : g->channels) total += mentionCount(cid);
    return total;
}

void Store::markRead(Snowflake channelId, Snowflake messageId) {
    auto &rs = readStates[channelId];
    if (messageId > rs.lastMessageId) rs.lastMessageId = messageId;
    rs.mentionCount = 0;
    emit unreadChanged(channelId);
}

QVector<VoiceState> Store::voiceStatesIn(Snowflake channelId) const {
    QVector<VoiceState> out;
    for (const auto &vs : voiceStates)
        if (vs.channelId == channelId) out.append(vs);
    std::sort(out.begin(), out.end(), [this](const VoiceState &a, const VoiceState &b) {
        return userDisplayName(a.userId, a.guildId).compare(userDisplayName(b.userId, b.guildId), Qt::CaseInsensitive) < 0;
    });
    return out;
}

const VoiceState *Store::voiceState(Snowflake userId) const {
    auto it = voiceStates.find(userId);
    return it == voiceStates.end() ? nullptr : &*it;
}

QVector<Snowflake> Store::typingIn(Snowflake channelId) {
    QVector<Snowflake> out;
    auto it = typing.find(channelId);
    if (it == typing.end()) return out;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto u = it->begin(); u != it->end();) {
        if (u.value() < now) u = it->erase(u);
        else {
            if (u.key() != self.id) out.append(u.key());
            ++u;
        }
    }
    return out;
}

QString Store::privateChannelName(const Channel &c) const {
    if (!c.name.isEmpty()) return c.name;
    QStringList names;
    for (auto r : c.recipients) {
        if (r == self.id) continue;
        names.append(userDisplayName(r));
    }
    return names.isEmpty() ? QString("Unnamed") : names.join(", ");
}

QVector<Snowflake> Store::sortedPrivateChannels() const {
    QVector<Snowflake> out = privateChannels;
    std::sort(out.begin(), out.end(), [this](Snowflake a, Snowflake b) {
        auto *ca = channel(a);
        auto *cb = channel(b);
        const Snowflake la = ca ? qMax(ca->lastMessageId, ca->id) : a;
        const Snowflake lb = cb ? qMax(cb->lastMessageId, cb->id) : b;
        return la > lb;
    });
    return out;
}

void Store::handleDispatch(const QString &t, const QJsonObject &d) {
    if (t == "READY") {
        handleReady(d);
    } else if (t == "READY_SUPPLEMENTAL") {
        handleReadySupplemental(d);
    } else if (t == "MESSAGE_CREATE") {
        Message m = parseMessage(d);
        if (auto it = channels.find(m.channelId); it != channels.end()) {
            it->lastMessageId = qMax(it->lastMessageId, m.id);
            if (it->isPrivate()) {
                privateChannels.removeAll(m.channelId);
                privateChannels.prepend(m.channelId);
                emit privateChannelsChanged();
            }
        }
        if (m.authorId == self.id) {
            auto &rs = readStates[m.channelId];
            rs.lastMessageId = m.id;
            rs.mentionCount = 0;
        } else {
            const bool mentioned = m.mentions.contains(self.id) || m.mentionEveryone
                || std::any_of(m.mentionRoles.begin(), m.mentionRoles.end(), [&](Snowflake r) { return memberRoles(m.guildId, self.id).contains(r); })
                || (channel(m.channelId) && channel(m.channelId)->isPrivate());
            if (mentioned && readStates.contains(m.channelId)) readStates[m.channelId].mentionCount++;
            else if (mentioned) readStates[m.channelId] = { 0, 1 };
        }
        typing[m.channelId].remove(m.authorId);
        emit messageCreated(m);
        emit unreadChanged(m.channelId);
        emit typingChanged(m.channelId);
    } else if (t == "MESSAGE_UPDATE") {
        if (has(d, "author")) upsertUser(obj(d, "author"));
        emit messageUpdated(id(d, "channel_id"), id(d), d);
    } else if (t == "MESSAGE_DELETE") {
        emit messageDeleted(id(d, "channel_id"), id(d));
    } else if (t == "MESSAGE_DELETE_BULK") {
        for (const auto &v : arr(d, "ids")) emit messageDeleted(id(d, "channel_id"), toId(v));
    } else if (t == "MESSAGE_ACK") {
        const Snowflake cid = id(d, "channel_id");
        auto &rs = readStates[cid];
        rs.lastMessageId = qMax(rs.lastMessageId, id(d, "message_id"));
        rs.mentionCount = i32(d, "mention_count", 0);
        emit unreadChanged(cid);
    } else if (t == "CHANNEL_UNREAD_UPDATE") {
        for (const auto &v : arr(d, "channel_unread_updates")) {
            const auto u = v.toObject();
            if (auto it = channels.find(id(u)); it != channels.end()) {
                it->lastMessageId = qMax(it->lastMessageId, id(u, "last_message_id"));
                emit unreadChanged(it->id);
            }
        }
    } else if (t == "MESSAGE_REACTION_ADD" || t == "MESSAGE_REACTION_REMOVE") {
        emit reactionChanged(id(d, "channel_id"), id(d, "message_id"), obj(d, "emoji"), t.endsWith("ADD") ? 1 : -1,
                             id(d, "user_id") == self.id);
    } else if (t == "MESSAGE_REACTION_REMOVE_ALL" || t == "MESSAGE_REACTION_REMOVE_EMOJI") {
        emit reactionsCleared(id(d, "channel_id"), id(d, "message_id"));
    } else if (t == "TYPING_START") {
        const Snowflake cid = id(d, "channel_id");
        const Snowflake uid = id(d, "user_id");
        if (has(d, "member")) storeMember(id(d, "guild_id"), obj(d, "member"), uid);
        typing[cid][uid] = QDateTime::currentMSecsSinceEpoch() + 10000;
        emit typingChanged(cid);
    } else if (t == "GUILD_CREATE") {
        ingestGuild(d);
        rebuildGuildOrder();
        emit guildsChanged();
        emit channelsChanged(id(d));
    } else if (t == "GUILD_UPDATE") {
        const QJsonObject o = flattenGuild(d);
        if (auto it = guilds.find(id(o)); it != guilds.end()) {
            const Guild fresh = parseGuild(o);
            it->name = fresh.name.isEmpty() ? it->name : fresh.name;
            if (has(o, "icon")) it->icon = fresh.icon;
            if (has(o, "owner_id")) it->ownerId = fresh.ownerId;
            if (has(o, "roles")) it->roles = fresh.roles;
            emit guildChanged(it->id);
        }
    } else if (t == "GUILD_DELETE") {
        const Snowflake gid = id(d);
        if (boolean(d, "unavailable")) {
            if (auto it = guilds.find(gid); it != guilds.end()) it->unavailable = true;
        } else {
            guilds.remove(gid);
            guildOrder.removeAll(gid);
        }
        emit guildsChanged();
    } else if (t == "CHANNEL_CREATE" || t == "CHANNEL_UPDATE" || t == "THREAD_CREATE" || t == "THREAD_UPDATE") {
        Channel c = parseChannel(d, id(d, "guild_id"));
        if (auto old = channels.find(c.id); old != channels.end() && c.lastMessageId == 0) c.lastMessageId = old->lastMessageId;
        channels.insert(c.id, c);
        if (c.isPrivate()) {
            if (!privateChannels.contains(c.id)) privateChannels.prepend(c.id);
            emit privateChannelsChanged();
        } else if (auto g = guilds.find(c.guildId); g != guilds.end()) {
            if (!t.startsWith("THREAD") && !g->channels.contains(c.id)) g->channels.append(c.id);
            emit channelsChanged(c.guildId);
        }
    } else if (t == "CHANNEL_DELETE" || t == "THREAD_DELETE") {
        const Snowflake cid = id(d);
        const Snowflake gid = id(d, "guild_id");
        channels.remove(cid);
        privateChannels.removeAll(cid);
        if (auto g = guilds.find(gid); g != guilds.end()) g->channels.removeAll(cid);
        if (gid) emit channelsChanged(gid);
        else emit privateChannelsChanged();
    } else if (t == "THREAD_LIST_SYNC") {
        for (const auto &v : arr(d, "threads")) {
            Channel c = parseChannel(v.toObject(), id(d, "guild_id"));
            channels.insert(c.id, c);
        }
    } else if (t == "GUILD_ROLE_CREATE" || t == "GUILD_ROLE_UPDATE") {
        const Snowflake gid = id(d, "guild_id");
        const auto r = obj(d, "role");
        if (auto g = guilds.find(gid); g != guilds.end()) {
            Role role;
            role.id = id(r);
            role.name = str(r, "name");
            role.color = static_cast<quint32>(i64(r, "color"));
            role.position = i32(r, "position");
            role.permissions = bits(r, "permissions");
            role.hoist = boolean(r, "hoist");
            g->roles.insert(role.id, role);
            emit channelsChanged(gid);
            emit membersChanged(gid);
        }
    } else if (t == "GUILD_ROLE_DELETE") {
        const Snowflake gid = id(d, "guild_id");
        if (auto g = guilds.find(gid); g != guilds.end()) {
            g->roles.remove(id(d, "role_id"));
            emit channelsChanged(gid);
        }
    } else if (t == "GUILD_MEMBER_UPDATE" || t == "GUILD_MEMBER_ADD") {
        const Snowflake gid = id(d, "guild_id");
        storeMember(gid, d);
        if (id(obj(d, "user")) == self.id) emit channelsChanged(gid);
        emit membersChanged(gid);
    } else if (t == "GUILD_MEMBERS_CHUNK") {
        const Snowflake gid = id(d, "guild_id");
        for (const auto &m : arr(d, "members")) storeMember(gid, m.toObject());
        for (const auto &p : arr(d, "presences")) setPresence(p.toObject());
        emit membersChanged(gid);
    } else if (t == "GUILD_MEMBER_LIST_UPDATE") {
        emit memberListUpdate(id(d, "guild_id"), d);
    } else if (t == "GUILD_EMOJIS_UPDATE") {
        if (auto g = guilds.find(id(d, "guild_id")); g != guilds.end()) {
            g->emojis.clear();
            for (const auto &v : arr(d, "emojis")) {
                const auto e = v.toObject();
                g->emojis.append({ id(e), str(e, "name"), boolean(e, "animated") });
            }
        }
    } else if (t == "PRESENCE_UPDATE") {
        if (has(d, "user") && obj(d, "user").size() > 1) upsertUser(obj(d, "user"));
        setPresence(d);
    } else if (t == "SESSIONS_REPLACE") {
        const auto sessions = d.value("sessions").isArray() ? arr(d, "sessions") : QJsonArray();
        QJsonArray list = sessions;
        if (list.isEmpty()) {
            // payload is sometimes the bare array wrapped by our dispatcher
            list = d.value("_array").toArray();
        }
        for (const auto &v : list) {
            const auto s = v.toObject();
            if (str(s, "session_id") == "all") {
                selfStatus = str(s, "status", selfStatus);
                emit selfChanged();
            }
        }
    } else if (t == "USER_UPDATE") {
        upsertUser(d);
        emit selfChanged();
    } else if (t == "VOICE_STATE_UPDATE") {
        setVoiceState(d, id(d, "guild_id"));
    } else if (t == "VOICE_SERVER_UPDATE") {
        emit voiceServerUpdate(id(d, "guild_id"), str(d, "endpoint"), str(d, "token"));
    } else if (t == "RELATIONSHIP_ADD") {
        if (i32(d, "type") == 1) relationshipsFriends.insert(id(d));
        if (has(d, "user")) upsertUser(obj(d, "user"));
    } else if (t == "RELATIONSHIP_REMOVE") {
        relationshipsFriends.remove(id(d));
    } else if (t == "USER_GUILD_SETTINGS_UPDATE") {
        const Snowflake gid = id(d, "guild_id");
        if (gid) {
            if (boolean(d, "muted")) mutedGuilds.insert(gid);
            else mutedGuilds.remove(gid);
        }
        for (const auto &ov : arr(d, "channel_overrides")) {
            const auto c = ov.toObject();
            if (boolean(c, "muted")) mutedChannels.insert(id(c, "channel_id"));
            else mutedChannels.remove(id(c, "channel_id"));
        }
        emit guildsChanged();
        if (gid) emit channelsChanged(gid);
    } else if (t == "USER_SETTINGS_PROTO_UPDATE") {
        const auto s = obj(d, "settings");
        if (i32(s, "type") == 1) {
            const auto f = decodeGuildFolders(str(s, "proto").toLatin1());
            if (!f.isEmpty()) {
                folders = f;
                rebuildGuildOrder();
                emit guildsChanged();
            }
        }
    }
}

} // namespace kestrel
