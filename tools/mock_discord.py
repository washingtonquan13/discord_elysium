#!/usr/bin/env python3
"""A small fake Discord used to exercise Kestrel end-to-end without a real account.

Implements enough of the REST API, the zlib-stream gateway, the QR remote-auth
gateway, the CDN and a voice gateway + UDP echo to drive every client code path.
Run:  python3 tools/mock_discord.py  (listens on 127.0.0.1:8765, UDP 8766)
"""
import asyncio, base64, io, json, os, random, struct, time, zlib, hashlib
from aiohttp import web, WSMsgType
from PIL import Image, ImageDraw
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding
import nacl.bindings as sodium

HOST, PORT, UDP_PORT = "127.0.0.1", 8765, 8766
BASE = f"http://{HOST}:{PORT}"
EPOCH = 1420070400000
LOG = []

def log(*a):
    line = " ".join(str(x) for x in a)
    LOG.append(line)
    print(line, flush=True)

_seq = [0]
def snow(ms=None):
    ms = int(time.time() * 1000) if ms is None else ms
    _seq[0] = (_seq[0] + 1) % 4096
    return str(((ms - EPOCH) << 22) | _seq[0])

def iso(ms):
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(ms / 1000)) + ".%03d+00:00" % (ms % 1000)

# ------------------------------------------------------------------ data
ME = {"id": "100000000000000001", "username": "washington", "global_name": "Washington", "discriminator": "0", "avatar": "a1"}
USERS = [
    {"id": "200000000000000001", "username": "mira", "global_name": "Mira", "discriminator": "0", "avatar": "b1"},
    {"id": "200000000000000002", "username": "theo", "global_name": "Theo", "discriminator": "0", "avatar": None},
    {"id": "200000000000000003", "username": "kestrelbot", "global_name": None, "discriminator": "0", "avatar": "c1", "bot": True},
    {"id": "200000000000000004", "username": "juniper", "global_name": "Juniper 🌿", "discriminator": "0", "avatar": "d1"},
    {"id": "200000000000000005", "username": "sam", "global_name": "Sam", "discriminator": "0", "avatar": "e1"},
]
U = {u["id"]: u for u in USERS + [ME]}
G1, G2, G3, G4 = "300000000000000001", "300000000000000002", "300000000000000003", "300000000000000004"
EVERY1 = G1
ROLE_MOD, ROLE_ART, ROLE_HIDDEN = "400000000000000001", "400000000000000002", "400000000000000003"

def role(id_, name, color, pos, perms="0", hoist=False):
    return {"id": id_, "name": name, "color": color, "position": pos, "permissions": perms, "hoist": hoist}

def ch(id_, name, type_=0, pos=0, parent=None, topic=None, overwrites=None, last=None, nsfw=False, limit=0):
    return {"id": id_, "name": name, "type": type_, "position": pos, "parent_id": parent, "topic": topic,
            "permission_overwrites": overwrites or [], "last_message_id": last, "nsfw": nsfw, "user_limit": limit}

C_GENERAL, C_ART, C_ANN, C_SECRET, C_VOICE, C_VOICE2, CAT_TEXT, CAT_VOICE, C_MEMES = (
    "500000000000000001", "500000000000000002", "500000000000000003", "500000000000000004",
    "500000000000000005", "500000000000000006", "500000000000000007", "500000000000000008", "500000000000000009")
VIEW = 1 << 10
GUILDS = [
    {
        "id": G1, "name": "Kestrel Test Server", "icon": "g1", "owner_id": USERS[0]["id"], "member_count": 1234,
        "roles": [role(G1, "@everyone", 0, 0, str((1 << 10) | (1 << 11) | (1 << 16) | (1 << 6) | (1 << 15) | (1 << 20) | (1 << 21))),
                  role(ROLE_MOD, "Moderators", 0x3498db, 3, "8192", True), role(ROLE_ART, "Artists", 0xe67e22, 2, "0", True),
                  role(ROLE_HIDDEN, "Hidden Club", 0x9b59b6, 1)],
        "emojis": [{"id": "600000000000000001", "name": "kestrel", "animated": False}],
        "channels": [
            ch(CAT_TEXT, "Text Channels", 4, 0), ch(CAT_VOICE, "Voice Channels", 4, 1),
            ch(C_ANN, "announcements", 5, 0, CAT_TEXT, "News and updates", last=snow(int(time.time()*1000) - 86400000 * 3)),
            ch(C_GENERAL, "general", 0, 1, CAT_TEXT, "Chat about anything **fun** — be nice!", last=None),
            ch(C_ART, "art-share", 0, 2, CAT_TEXT, "Post your art"),
            ch(C_MEMES, "memes", 0, 3, CAT_TEXT),
            ch(C_SECRET, "mods-only", 0, 4, CAT_TEXT, overwrites=[{"id": G1, "type": 0, "allow": "0", "deny": str(VIEW)}]),
            ch(C_VOICE, "Lounge", 2, 0, CAT_VOICE), ch(C_VOICE2, "Gaming", 2, 1, CAT_VOICE, limit=5),
        ],
        "threads": [], "large": False, "lazy": True,
    },
    {"properties": {"id": G2, "name": "Retro Games Club", "icon": None, "owner_id": ME["id"]}, "id": G2,
     "roles": [role(G2, "@everyone", 0, 0, str(VIEW | (1 << 11) | (1 << 16)))], "emojis": [],
     "channels": [ch("510000000000000001", "lobby", 0, 0, None, "Say hi")], "member_count": 42},
    {"id": G3, "name": "Pixel Art Collective", "icon": "g3", "owner_id": USERS[1]["id"],
     "roles": [role(G3, "@everyone", 0, 0, str(VIEW | (1 << 11) | (1 << 16)))], "emojis": [],
     "channels": [ch("520000000000000001", "showcase", 0, 0, None)], "member_count": 800},
    {"id": G4, "name": "Study Group", "icon": "g4", "owner_id": USERS[1]["id"],
     "roles": [role(G4, "@everyone", 0, 0, str(VIEW | (1 << 11) | (1 << 16)))], "emojis": [],
     "channels": [ch("530000000000000001", "homework-help", 0, 0, None)], "member_count": 25},
]
DM1, DM2, GDM = "700000000000000001", "700000000000000002", "700000000000000003"
PRIVATE = [
    {"id": DM1, "type": 1, "recipient_ids": [USERS[0]["id"]], "last_message_id": None},
    {"id": DM2, "type": 1, "recipient_ids": [USERS[1]["id"]], "last_message_id": None},
    {"id": GDM, "type": 3, "name": None, "recipient_ids": [USERS[0]["id"], USERS[1]["id"], USERS[3]["id"]], "last_message_id": None, "icon": None},
]
CHANNEL_GUILD = {}
for g in GUILDS:
    for c in g["channels"]:
        CHANNEL_GUILD[c["id"]] = g["id"]

MESSAGES = {}  # channel -> list (oldest first)

def author(u):
    return {k: v for k, v in u.items()}

def make_msg(cid, u, content, ms, **extra):
    m = {"id": snow(ms), "channel_id": cid, "author": author(u), "content": content, "timestamp": iso(ms),
         "edited_timestamp": None, "type": 0, "flags": 0, "mentions": [], "mention_roles": [], "mention_everyone": False,
         "attachments": [], "embeds": [], "pinned": False}
    if cid in CHANNEL_GUILD:
        m["guild_id"] = CHANNEL_GUILD[cid]
    m.update(extra)
    return m

def seed():
    now = int(time.time() * 1000)
    t = now - 3600_000 * 30
    lines = [
        (USERS[0], "hey everyone! welcome to the **test server** 👋"),
        (USERS[1], "yo. did anyone try the new build? it's *so* much lighter on RAM"),
        (USERS[3], "`kestrel.exe` sitting at like 90 MB for me, the old client was 900+"),
        (USERS[0], "nice. here's the snippet I used:\n```cpp\nint main() {\n    return 0; // hello\n}\n```"),
        (USERS[4], "> quoting the classics\nbut also ~~strikethrough~~ and __underline__ and ||spoilers||"),
        (USERS[1], "check <#%s> for the art dump, and ping <@&%s> if something breaks" % (C_ART, ROLE_MOD)),
        (USERS[2], "Build finished ✅ <:kestrel:600000000000000001>"),
        (USERS[3], "🎉🎉🎉"),
        (USERS[0], "Meeting is <t:%d:R> — see https://example.com/agenda for the plan" % (now // 1000 + 7200)),
    ]
    msgs = []
    for i in range(140):
        u, text = lines[i % len(lines)]
        if i >= len(lines):
            text = f"message number {i} from {u['global_name'] or u['username']} — scrolling test"
        t += random.randint(20_000, 900_000)
        msgs.append(make_msg(C_GENERAL, u, text, t))
    # rich content near the end
    t = now - 3600_000 * 2
    img = make_msg(C_GENERAL, USERS[3], "look at this sunset I painted", t,
                   attachments=[{"id": snow(t), "filename": "sunset.png", "size": 482133, "content_type": "image/png",
                                 "width": 1600, "height": 1000, "url": f"https://cdn.discordapp.com/attachments/1/2/sunset.png?ex=1",
                                 "proxy_url": f"https://media.discordapp.net/attachments/1/2/sunset.png?ex=1"}],
                   reactions=[{"emoji": {"id": None, "name": "🔥"}, "count": 3, "me": True},
                              {"emoji": {"id": "600000000000000001", "name": "kestrel"}, "count": 1, "me": False}])
    msgs.append(img)
    t += 60_000
    msgs.append(make_msg(C_GENERAL, USERS[1], "wow that's gorgeous", t, type=19,
                         message_reference={"message_id": img["id"], "channel_id": C_GENERAL},
                         referenced_message=img))
    t += 60_000
    msgs.append(make_msg(C_GENERAL, USERS[2], "", t, embeds=[{
        "type": "rich", "title": "Build #1024 passed", "url": "https://example.com/ci/1024", "color": 0x23a55a,
        "description": "All **37** checks passed in `2m 14s`.", "author": {"name": "CI Bot"},
        "fields": [{"name": "Branch", "value": "main", "inline": True}, {"name": "Commit", "value": "`a1b2c3d`", "inline": True},
                   {"name": "Duration", "value": "2m 14s", "inline": True}],
        "footer": {"text": "kestrel-ci"}, "thumbnail": {"url": "https://cdn.discordapp.com/icons/1/thumb.png", "proxy_url": "https://media.discordapp.net/icons/1/thumb.png", "width": 128, "height": 128}}]))
    t += 60_000
    msgs.append(make_msg(C_GENERAL, USERS[0], "hey <@%s>, can you review the PR? also a file:" % ME["id"], t,
                         mentions=[author(ME)],
                         attachments=[{"id": snow(t), "filename": "notes.txt", "size": 2048, "content_type": "text/plain",
                                       "url": "https://cdn.discordapp.com/attachments/1/3/notes.txt"}]))
    t += 30_000
    msgs.append(make_msg(C_GENERAL, ME, "sure, on it", t))
    t += 10_000
    msgs.append(make_msg(C_GENERAL, USERS[4], "", t, type=7))
    MESSAGES[C_GENERAL] = msgs
    MESSAGES[C_ART] = [make_msg(C_ART, USERS[3], "daily sketch #%d" % i, now - 86400_000 * (10 - i),
                                attachments=[{"id": snow(), "filename": f"sketch{i}.png", "size": 90000, "content_type": "image/png",
                                              "width": 800, "height": 1200, "url": f"https://cdn.discordapp.com/attachments/1/9/sketch{i}.png",
                                              "proxy_url": f"https://media.discordapp.net/attachments/1/9/sketch{i}.png"}]) for i in range(8)]
    MESSAGES[DM1] = [make_msg(DM1, USERS[0], "hey! are you coming tonight?", now - 7200_000), make_msg(DM1, ME, "yep, 8pm", now - 7000_000)]
    MESSAGES[GDM] = [make_msg(GDM, USERS[3], "group chat test", now - 50_000)]
    for cid, lst in MESSAGES.items():
        last = lst[-1]["id"]
        for g in GUILDS:
            for c in g["channels"]:
                if c["id"] == cid:
                    c["last_message_id"] = last
        for p in PRIVATE:
            if p["id"] == cid:
                p["last_message_id"] = last

seed()

# ------------------------------------------------------------------ protobuf (guild folders)
def varint(n):
    out = b""
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out += bytes([b | 0x80])
        else:
            return out + bytes([b])

def field(num, wt, payload):
    tag = varint((num << 3) | wt)
    if wt == 2:
        return tag + varint(len(payload)) + payload
    return tag + payload

def folders_proto():
    def folder(ids, fid=None, name=None, color=None):
        packed = b"".join(struct.pack("<Q", int(i)) for i in ids)
        f = field(1, 2, packed)
        if fid is not None:
            f += field(2, 2, field(1, 0, varint(fid)))
        if name is not None:
            f += field(3, 2, field(1, 2, name.encode()))
        if color is not None:
            f += field(4, 2, field(1, 0, varint(color)))
        return f
    gf = field(1, 2, folder([G1])) + field(1, 2, folder([G3, G4], 12345, "Art & Study", 0xe67e22)) + field(1, 2, folder([G2]))
    return base64.b64encode(field(14, 2, gf) + field(1, 2, b"\x08\x01")).decode()

# ------------------------------------------------------------------ gateway
CLIENTS = set()

class GatewayConn:
    def __init__(self, ws):
        self.ws = ws
        self.seq = 0
        self.z = zlib.compressobj()
        self.session = "sess_" + hashlib.md5(os.urandom(8)).hexdigest()[:12]

    async def send(self, op, d=None, t=None):
        msg = {"op": op, "d": d}
        if op == 0:
            self.seq += 1
            msg["s"] = self.seq
            msg["t"] = t
        data = self.z.compress(json.dumps(msg).encode()) + self.z.flush(zlib.Z_SYNC_FLUSH)
        await self.ws.send_bytes(data)

    async def dispatch(self, t, d):
        await self.send(0, d, t)

async def broadcast(t, d):
    for c in list(CLIENTS):
        try:
            await c.dispatch(t, d)
        except Exception:
            CLIENTS.discard(c)

def ready_payload(conn):
    return {
        "v": 9, "user": ME, "session_id": conn.session, "resume_gateway_url": f"ws://{HOST}:{PORT}",
        "users": USERS, "guilds": GUILDS, "private_channels": PRIVATE,
        "merged_members": [[{"user_id": ME["id"], "roles": [ROLE_MOD], "nick": None}], [{"user_id": ME["id"], "roles": []}], [], []],
        "read_state": {"entries": [
            {"id": C_GENERAL, "last_message_id": MESSAGES[C_GENERAL][-8]["id"], "mention_count": 1},
            {"id": C_ART, "last_message_id": "0", "mention_count": 0},
            {"id": C_ANN, "last_message_id": "999999999999999999999", "mention_count": 0},
            {"id": DM1, "last_message_id": MESSAGES[DM1][0]["id"], "mention_count": 1},
            {"id": GDM, "last_message_id": "0", "mention_count": 0},
        ], "partial": False, "version": 1},
        "user_guild_settings": {"entries": [{"guild_id": G4, "muted": True, "channel_overrides": []},
                                            {"guild_id": G1, "muted": False, "channel_overrides": [{"channel_id": C_MEMES, "muted": True}]}]},
        "user_settings_proto": folders_proto(),
        "relationships": [{"id": USERS[0]["id"], "type": 1, "user": USERS[0]}],
        "sessions": [{"session_id": "all", "status": "online", "activities": []}],
        "auth_token": None,
        "country_code": "US",
    }

VOICE_STATES = {}  # user -> state

@web.middleware
async def logger(request, handler):
    resp = await handler(request)
    if not request.path.startswith(("/cdn", "/media")):
        log("HTTP", request.method, request.path_qs, "->", resp.status)
    return resp

async def gateway(request):
    ws = web.WebSocketResponse(max_msg_size=0)
    await ws.prepare(request)
    conn = GatewayConn(ws)
    log("GW connect ua=", request.headers.get("User-Agent", "")[:40], "origin=", request.headers.get("Origin"))
    await conn.send(10, {"heartbeat_interval": 41250})
    async for msg in ws:
        if msg.type != WSMsgType.TEXT:
            continue
        p = json.loads(msg.data)
        op, d = p["op"], p.get("d")
        if op == 1:
            await conn.send(11)
        elif op == 2:
            log("GW IDENTIFY caps=", d.get("capabilities"), "build=", d["properties"].get("client_build_number"),
                "browser=", d["properties"].get("browser"), "token=", d.get("token", "")[:8] + "…")
            if d.get("token") != "mock-token":
                await ws.close(code=4004, message=b"Authentication failed")
                break
            CLIENTS.add(conn)
            await conn.dispatch("READY", ready_payload(conn))
            await conn.dispatch("READY_SUPPLEMENTAL", {
                "merged_presences": {"guilds": [[{"user_id": USERS[0]["id"], "status": "online"}, {"user_id": USERS[1]["id"], "status": "idle"}]],
                                     "friends": [{"user_id": USERS[0]["id"], "status": "online"}, {"user_id": USERS[1]["id"], "status": "dnd"}]},
                "guilds": [{"id": G1, "voice_states": [
                    {"user_id": USERS[1]["id"], "channel_id": C_VOICE, "session_id": "x", "self_mute": True, "self_deaf": False, "mute": False, "deaf": False,
                     "member": {"user": USERS[1], "roles": [ROLE_ART]}},
                    {"user_id": USERS[3]["id"], "channel_id": C_VOICE, "session_id": "y", "self_mute": False, "self_deaf": False, "mute": False, "deaf": False, "self_stream": True,
                     "member": {"user": USERS[3], "roles": []}}]}, {"id": G2}, {"id": G3}, {"id": G4}],
                "merged_members": [[{"user_id": USERS[1]["id"], "roles": [ROLE_ART]}, {"user_id": USERS[0]["id"], "roles": [ROLE_MOD], "nick": "Mira (mod)"}], [], [], []],
            })
            await conn.dispatch("SESSIONS_REPLACE", [{"session_id": "all", "status": "online"}])
            await conn.dispatch("CHANNEL_UNREAD_UPDATE", {"guild_id": G1, "channel_unread_updates": [{"id": C_ART, "last_message_id": MESSAGES[C_ART][-1]["id"]}]})
        elif op == 6:
            log("GW RESUME", request.path, "seq", d.get("seq"), "session", d.get("session_id"))
            CLIENTS.add(conn)
            await conn.dispatch("RESUMED", {})
        elif op == 37:
            subs = d.get("subscriptions", {})
            log("GW SUBSCRIBE", json.dumps(subs)[:160])
            for gid, sub in subs.items():
                if gid == G1 and sub.get("channels"):
                    items = [{"group": {"id": ROLE_MOD, "count": 1}},
                             {"member": {"user": USERS[0], "roles": [ROLE_MOD], "nick": "Mira (mod)", "presence": {"status": "online", "activities": [{"type": 4, "state": "building things"}]}}},
                             {"group": {"id": ROLE_ART, "count": 2}},
                             {"member": {"user": USERS[1], "roles": [ROLE_ART], "presence": {"status": "idle", "activities": [{"type": 0, "name": "Hades II"}]}}},
                             {"member": {"user": USERS[3], "roles": [ROLE_ART], "presence": {"status": "dnd", "activities": []}}},
                             {"group": {"id": "online", "count": 2}},
                             {"member": {"user": USERS[2], "roles": [], "presence": {"status": "online", "activities": [{"type": 2, "name": "Spotify"}]}}},
                             {"member": {"user": ME, "roles": [ROLE_MOD], "presence": {"status": "online", "activities": []}}},
                             {"group": {"id": "offline", "count": 1}},
                             {"member": {"user": USERS[4], "roles": [], "presence": {"status": "offline", "activities": []}}}]
                    await conn.dispatch("GUILD_MEMBER_LIST_UPDATE", {"guild_id": G1, "id": "everyone", "member_count": 5, "online_count": 4,
                                                                     "groups": [{"id": ROLE_MOD, "count": 1}, {"id": ROLE_ART, "count": 2}, {"id": "online", "count": 2}, {"id": "offline", "count": 1}],
                                                                     "ops": [{"op": "SYNC", "range": [0, 99], "items": items}]})
        elif op == 8:
            log("GW REQUEST_MEMBERS", d.get("guild_id"), len(d.get("user_ids", [])))
            gid = d.get("guild_id")
            members = []
            for uid in d.get("user_ids", []):
                if uid in U:
                    roles = [ROLE_ART] if uid in (USERS[1]["id"], USERS[3]["id"]) else ([ROLE_MOD] if uid == USERS[0]["id"] else [])
                    members.append({"user": U[uid], "roles": roles, "nick": "Mira (mod)" if uid == USERS[0]["id"] else None})
            await conn.dispatch("GUILD_MEMBERS_CHUNK", {"guild_id": gid, "members": members, "chunk_index": 0, "chunk_count": 1})
        elif op == 3:
            log("GW PRESENCE", d.get("status"))
            await conn.dispatch("SESSIONS_REPLACE", [{"session_id": "all", "status": d.get("status")}])
        elif op == 4:
            log("GW VOICE_STATE_UPDATE", d)
            gid, cid = d.get("guild_id"), d.get("channel_id")
            st = {"user_id": ME["id"], "guild_id": gid, "channel_id": cid, "session_id": conn.session,
                  "self_mute": d.get("self_mute"), "self_deaf": d.get("self_deaf"), "mute": False, "deaf": False,
                  "member": {"user": ME, "roles": [ROLE_MOD]}}
            await conn.dispatch("VOICE_STATE_UPDATE", st)
            if cid and VOICE_STATES.get(ME["id"]) != cid:
                await conn.dispatch("VOICE_SERVER_UPDATE", {"token": "voice-token", "guild_id": gid, "endpoint": f"{HOST}:{PORT}/voice"})
            VOICE_STATES[ME["id"]] = cid
    CLIENTS.discard(conn)
    log("GW closed")
    return ws

# ------------------------------------------------------------------ voice
class VoiceUdp(asyncio.DatagramProtocol):
    key = None
    ssrc = 1111
    stats = {"rx": 0, "ok": 0, "bad": 0, "discovery": 0}
    def connection_made(self, transport):
        self.transport = transport
    def datagram_received(self, data, addr):
        if len(data) == 74 and data[0:2] == b"\x00\x01":
            VoiceUdp.stats["discovery"] += 1
            resp = bytearray(74)
            resp[0:2] = b"\x00\x02"; resp[2:4] = b"\x00\x46"; resp[4:8] = data[4:8]
            ip = addr[0].encode(); resp[8:8 + len(ip)] = ip
            resp[72:74] = struct.pack(">H", addr[1])
            self.transport.sendto(bytes(resp), addr)
            return
        if len(data) == 2:
            return  # keepalive
        VoiceUdp.stats["rx"] += 1
        if not VoiceUdp.key:
            return
        header, nonce4 = data[:12], data[-4:]
        try:
            plain = sodium.crypto_aead_xchacha20poly1305_ietf_decrypt(data[12:-4], header, nonce4 + b"\x00" * 20, VoiceUdp.key)
            VoiceUdp.stats["ok"] += 1
        except Exception:
            VoiceUdp.stats["bad"] += 1
            return
        # echo it back as another speaker (ssrc 2222) so the client decodes & mixes it
        hdr = bytearray(header); hdr[8:12] = struct.pack(">I", 2222)
        n = os.urandom(4)
        enc = sodium.crypto_aead_xchacha20poly1305_ietf_encrypt(plain, bytes(hdr), n + b"\x00" * 20, VoiceUdp.key)
        self.transport.sendto(bytes(hdr) + enc + n, addr)

async def voice(request):
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    log("VOICE ws connect", request.path_qs)
    await ws.send_json({"op": 8, "d": {"heartbeat_interval": 13750}})
    async for msg in ws:
        if msg.type == WSMsgType.BINARY:
            log("VOICE binary op", msg.data[0])
            continue
        if msg.type != WSMsgType.TEXT:
            continue
        p = json.loads(msg.data)
        op, d = p["op"], p.get("d")
        if op == 0:
            log("VOICE IDENTIFY", {k: d.get(k) for k in ("server_id", "user_id", "session_id", "token", "max_dave_protocol_version")})
            await ws.send_json({"op": 2, "d": {"ssrc": 3333, "ip": HOST, "port": UDP_PORT, "modes": ["aead_xchacha20_poly1305_rtpsize", "aead_aes256_gcm_rtpsize"]}})
        elif op == 1:
            log("VOICE SELECT_PROTOCOL", d.get("mode"), d.get("address"), d.get("port"))
            VoiceUdp.key = os.urandom(32)
            await ws.send_json({"op": 4, "seq": 1, "d": {"mode": "aead_xchacha20_poly1305_rtpsize", "secret_key": list(VoiceUdp.key), "dave_protocol_version": 0}})
            await ws.send_json({"op": 5, "seq": 2, "d": {"user_id": USERS[3]["id"], "ssrc": 2222, "speaking": 1}})
        elif op == 3:
            await ws.send_json({"op": 6, "d": {"t": d.get("t")}})
        elif op == 5:
            log("VOICE SPEAKING", d)
    log("VOICE closed; udp stats", VoiceUdp.stats)
    return ws

# ------------------------------------------------------------------ remote auth (QR)
async def remote_auth(request):
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    await ws.send_json({"op": "hello", "heartbeat_interval": 41250, "timeout_ms": 120000})
    pub = None
    async for msg in ws:
        if msg.type != WSMsgType.TEXT:
            continue
        p = json.loads(msg.data)
        if p["op"] == "init":
            pub = serialization.load_der_public_key(base64.b64decode(p["encoded_public_key"]))
            nonce = os.urandom(32)
            request.app["ra_nonce"] = nonce
            enc = pub.encrypt(nonce, padding.OAEP(mgf=padding.MGF1(hashes.SHA256()), algorithm=hashes.SHA256(), label=None))
            await ws.send_json({"op": "nonce_proof", "encrypted_nonce": base64.b64encode(enc).decode()})
        elif p["op"] == "nonce_proof":
            expected = base64.urlsafe_b64encode(request.app["ra_nonce"]).decode().rstrip("=")
            ok = p["nonce"] == expected
            log("RA nonce proof", "OK" if ok else "MISMATCH")
            if not ok:
                await ws.close(code=4003)
                break
            der = pub.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
            fp = base64.urlsafe_b64encode(hashlib.sha256(der).digest()).decode().rstrip("=")
            await ws.send_json({"op": "pending_remote_init", "fingerprint": fp})
            request.app["ra_pub"] = pub
            if os.environ.get("MOCK_QR_AUTOSCAN"):
                await asyncio.sleep(1.5)
                payload = f"{ME['id']}:0:a1:{ME['username']}".encode()
                enc = pub.encrypt(payload, padding.OAEP(mgf=padding.MGF1(hashes.SHA256()), algorithm=hashes.SHA256(), label=None))
                await ws.send_json({"op": "pending_ticket", "encrypted_user_payload": base64.b64encode(enc).decode()})
                await asyncio.sleep(1.5)
                await ws.send_json({"op": "pending_login", "ticket": "ticket-123"})
    return ws

async def ra_login(request):
    body = await request.json()
    pub = request.app.get("ra_pub")
    if body.get("ticket") != "ticket-123" or not pub:
        return web.json_response({"message": "bad ticket"}, status=400)
    enc = pub.encrypt(b"mock-token", padding.OAEP(mgf=padding.MGF1(hashes.SHA256()), algorithm=hashes.SHA256(), label=None))
    return web.json_response({"encrypted_token": base64.b64encode(enc).decode()})

# ------------------------------------------------------------------ REST
def auth_ok(request):
    return request.headers.get("Authorization") == "mock-token"

async def app_page(request):
    return web.Response(text='<html><script src="/assets/sentry.a1b2c3.js"></script><script src="/assets/web.x.js"></script></html>',
                        content_type="text/html", headers={"Set-Cookie": "__dcfduid=abc; Path=/"})

async def asset(request):
    return web.Response(text='window.GLOBAL_ENV={};e.buildNumber","412345"', content_type="application/javascript")

async def get_messages(request):
    if not auth_ok(request):
        return web.json_response({"message": "401: Unauthorized"}, status=401)
    cid = request.match_info["cid"]
    limit = int(request.query.get("limit", 50))
    before, around = request.query.get("before"), request.query.get("around")
    lst = MESSAGES.get(cid, [])
    if before:
        lst = [m for m in lst if int(m["id"]) < int(before)]
        out = lst[-limit:]
    elif around:
        idx = next((i for i, m in enumerate(lst) if m["id"] == around), len(lst) - 1)
        out = lst[max(0, idx - limit // 2): idx + limit // 2]
    else:
        out = lst[-limit:]
    await asyncio.sleep(0.05)
    return web.json_response(list(reversed(out)))

async def post_message(request):
    if not auth_ok(request):
        return web.json_response({"message": "401: Unauthorized"}, status=401)
    cid = request.match_info["cid"]
    if request.content_type.startswith("multipart"):
        reader = await request.multipart()
        body, files = {}, []
        async for part in reader:
            if part.name == "payload_json":
                body = json.loads(await part.text())
            else:
                data = await part.read()
                files.append({"id": snow(), "filename": part.filename, "size": len(data), "content_type": "image/png" if part.filename.endswith(".png") else "application/octet-stream",
                              "url": f"https://cdn.discordapp.com/attachments/1/7/{part.filename}", "proxy_url": f"https://media.discordapp.net/attachments/1/7/{part.filename}",
                              "width": 640, "height": 480})
        log("UPLOAD", [f["filename"] for f in files])
    else:
        body, files = await request.json(), []
    log("SEND", cid, repr(body.get("content"))[:80], "nonce=", body.get("nonce"), "reply=", body.get("message_reference"))
    if body.get("content") == "fail please":
        return web.json_response({"message": "Cannot send messages to this user", "code": 50007}, status=403)
    m = make_msg(cid, ME, body.get("content", ""), int(time.time() * 1000), attachments=files, nonce=body.get("nonce"))
    ref = body.get("message_reference")
    if ref:
        orig = next((x for x in MESSAGES.get(cid, []) if x["id"] == ref["message_id"]), None)
        m.update(type=19, message_reference=ref, referenced_message=orig)
    MESSAGES.setdefault(cid, []).append(m)
    await broadcast("MESSAGE_CREATE", m)
    # a friendly bot replies to "!ping"
    if body.get("content", "").strip() == "!ping":
        async def later():
            await broadcast("TYPING_START", {"channel_id": cid, "user_id": USERS[2]["id"], "timestamp": int(time.time()), "guild_id": CHANNEL_GUILD.get(cid)})
            await asyncio.sleep(1.2)
            r = make_msg(cid, USERS[2], "pong! 🏓", int(time.time() * 1000))
            MESSAGES[cid].append(r)
            await broadcast("MESSAGE_CREATE", r)
        asyncio.ensure_future(later())
    return web.json_response(m)

async def patch_message(request):
    cid, mid = request.match_info["cid"], request.match_info["mid"]
    body = await request.json()
    for m in MESSAGES.get(cid, []):
        if m["id"] == mid:
            m["content"] = body["content"]
            m["edited_timestamp"] = iso(int(time.time() * 1000))
            await broadcast("MESSAGE_UPDATE", m)
            log("EDIT", mid, body["content"])
            return web.json_response(m)
    return web.json_response({}, status=404)

async def delete_message(request):
    cid, mid = request.match_info["cid"], request.match_info["mid"]
    MESSAGES[cid] = [m for m in MESSAGES.get(cid, []) if m["id"] != mid]
    await broadcast("MESSAGE_DELETE", {"id": mid, "channel_id": cid, "guild_id": CHANNEL_GUILD.get(cid)})
    log("DELETE", mid)
    return web.Response(status=204)

async def reaction(request):
    cid, mid, emoji = request.match_info["cid"], request.match_info["mid"], request.match_info["emoji"]
    name, _, eid = emoji.partition(":")
    e = {"id": eid or None, "name": name}
    t = "MESSAGE_REACTION_ADD" if request.method == "PUT" else "MESSAGE_REACTION_REMOVE"
    log("REACTION", request.method, emoji)
    await broadcast(t, {"user_id": ME["id"], "channel_id": cid, "message_id": mid, "emoji": e, "guild_id": CHANNEL_GUILD.get(cid)})
    return web.Response(status=204)

async def typing(request):
    return web.Response(status=204)

async def ack(request):
    log("ACK", request.match_info["cid"], request.match_info["mid"])
    await broadcast("MESSAGE_ACK", {"channel_id": request.match_info["cid"], "message_id": request.match_info["mid"], "version": 1})
    return web.json_response({"token": None})

async def create_dm(request):
    body = await request.json()
    uid = body["recipients"][0]
    c = {"id": snow(), "type": 1, "recipients": [U[uid]], "last_message_id": None}
    PRIVATE.append({"id": c["id"], "type": 1, "recipient_ids": [uid]})
    return web.json_response(c)

# ------------------------------------------------------------------ CDN (generated images)
PALETTE = [(88, 101, 242), (35, 165, 90), (240, 178, 50), (242, 63, 67), (235, 69, 158), (0, 168, 252), (230, 126, 34)]
async def cdn(request):
    path = request.path
    size = int(request.query.get("size", 0) or 0)
    w = int(request.query.get("width", 0) or 0) or size or 256
    h = int(request.query.get("height", 0) or 0) or size or 256
    if "attachments" in path and not request.query.get("width"):
        w, h = 1600, 1000
    col = PALETTE[int(hashlib.md5(path.encode()).hexdigest(), 16) % len(PALETTE)]
    img = Image.new("RGB", (w, h), col)
    d = ImageDraw.Draw(img)
    if "attachments" in path or "thumb" in path:
        for i in range(0, h, max(2, h // 40)):  # gradient "painting"
            t = i / h
            d.line([(0, i), (w, i)], fill=(int(col[0] * (1 - t) + 30 * t), int(col[1] * (1 - t) + 20 * t), int(col[2] * (1 - t) + 60 * t)))
        d.ellipse([w * 0.6, h * 0.15, w * 0.8, h * 0.15 + w * 0.2], fill=(255, 220, 120))
    else:
        d.ellipse([w * 0.3, h * 0.2, w * 0.7, h * 0.6], fill=(255, 255, 255))
        d.rectangle([w * 0.2, h * 0.65, w * 0.8, h], fill=(255, 255, 255))
    buf = io.BytesIO()
    img.save(buf, "PNG")
    return web.Response(body=buf.getvalue(), content_type="image/png", headers={"Cache-Control": "max-age=86400"})

async def main():
    app = web.Application(middlewares=[logger], client_max_size=50 * 1024 * 1024)
    app.router.add_get("/app", app_page)
    app.router.add_get("/assets/{name}", asset)
    app.router.add_get("/gateway", gateway)
    app.router.add_get("/", gateway)  # resume_gateway_url has no path, like Discord's
    async def drop(request):
        for c in list(CLIENTS):
            await c.ws.close(code=int(request.query.get("code", 4000)))
        return web.Response(text="dropped")
    app.router.add_get("/debug/drop", drop)
    app.router.add_get("/voice/", voice)
    app.router.add_get("/voice", voice)
    app.router.add_get("/remote-auth", remote_auth)
    app.router.add_post("/api/v9/users/@me/remote-auth/login", ra_login)
    app.router.add_post("/api/v9/users/@me/channels", create_dm)
    app.router.add_get("/api/v9/channels/{cid}/messages", get_messages)
    app.router.add_post("/api/v9/channels/{cid}/messages", post_message)
    app.router.add_patch("/api/v9/channels/{cid}/messages/{mid}", patch_message)
    app.router.add_delete("/api/v9/channels/{cid}/messages/{mid}", delete_message)
    app.router.add_put("/api/v9/channels/{cid}/messages/{mid}/reactions/{emoji}/@me", reaction)
    app.router.add_delete("/api/v9/channels/{cid}/messages/{mid}/reactions/{emoji}/0/@me", reaction)
    app.router.add_post("/api/v9/channels/{cid}/typing", typing)
    app.router.add_post("/api/v9/channels/{cid}/messages/{mid}/ack", ack)
    app.router.add_get("/cdn/{tail:.*}", cdn)
    app.router.add_get("/media/{tail:.*}", cdn)
    runner = web.AppRunner(app)
    await runner.setup()
    await web.TCPSite(runner, HOST, PORT).start()
    loop = asyncio.get_running_loop()
    await loop.create_datagram_endpoint(VoiceUdp, local_addr=(HOST, UDP_PORT))
    log(f"mock discord on {BASE}")

    async def chatter():
        n = 0
        while True:
            await asyncio.sleep(9)
            n += 1
            u = USERS[n % 2]
            await broadcast("TYPING_START", {"channel_id": C_GENERAL, "user_id": u["id"], "timestamp": int(time.time()), "guild_id": G1})
            await asyncio.sleep(2)
            m = make_msg(C_GENERAL, u, f"live message {n} ✨", int(time.time() * 1000))
            MESSAGES[C_GENERAL].append(m)
            await broadcast("MESSAGE_CREATE", m)
    if not os.environ.get("MOCK_QUIET"):
        asyncio.ensure_future(chatter())
    await asyncio.Event().wait()

if __name__ == "__main__":
    asyncio.run(main())
