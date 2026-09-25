# Kestrel

A lightweight Discord client for Windows, written in C++ with Qt Quick. It is
not Electron and doesn't embed a browser: it draws its UI natively with the GPU,
decodes images at the size they're shown, and keeps only a few channels of
messages in memory.

> Kestrel is an unofficial third-party client. Discord's terms discourage
> them, and Discord sometimes flags accounts that use one (usually for joining
> or leaving servers, or frequent reconnects). Kestrel copies the official web
> client's behaviour as closely as it can, but use it at your own risk.

## Running it

1. Unzip `Kestrel-windows-x64.zip` anywhere (for example `C:\Apps\Kestrel`).
2. Run `Kestrel.exe`.
3. Scan the QR code with the Discord mobile app (Profile → Scan QR code), or
   paste an account token.

Your token is stored encrypted with Windows' Data Protection API, so only
your Windows account can read it. It's in
`%APPDATA%\Kestrel\Kestrel\token.bin`. To sign out, go to
**Settings → Log Out**.

## What works

- Logging in with a QR code or a token, auto-reconnect and resume
- Servers (including folders), channels and categories, permissions and
  hidden channels, unread and mention indicators, muted servers and channels
- Direct messages and group DMs
- Messages with Discord markdown: bold, italics, underline, strikethrough,
  spoilers, code blocks, quotes, headers, lists, links, mentions, custom emoji
  and timestamps
- Replies (with the @ on or off), editing (↑ edits your last message),
  deleting, reactions, typing indicators, jump to a replied message
- Image attachments and embeds (fetched pre-shrunk from Discord's media proxy),
  file attachments, drag-and-drop or button uploads
- Emoji picker and `:shortcode:` autocomplete, `@mention` and `#channel`
  autocomplete
- A member list with roles, statuses and activities, and profile popups
- **Voice chat**, including Discord's end-to-end encryption (DAVE),
  voice activity or push-to-talk (works in games), mute and deafen,
  per-user volume, speaking indicators, and a mic test
- Quick switcher (**Ctrl+K**), **Alt+↑/↓** to move between channels, desktop
  notifications for DMs and mentions, and a minimise-to-tray option

## Not yet supported

- Video, screen sharing and watching streams (people who are streaming show a
  LIVE badge)
- Threads and forum channels
- Stickers (their names are shown), polls, and slash-command interactions
- Changing your profile or server settings, or joining and leaving servers
  (do these in the official app; they're also what most often trips the spam
  filter)

## Troubleshooting

Kestrel writes a log to `%APPDATA%\Kestrel\Kestrel\kestrel.log`. If something
misbehaves, the last few dozen lines usually show why. The log never contains
your token.

Images are cached in `%LOCALAPPDATA%\Kestrel\Kestrel\cache`. You can delete
this folder at any time.

## Building from source

Kestrel needs Qt 6.4 or newer (Core, Gui, Network, WebSockets, Qml, Quick,
QuickControls2, Widgets, Svg), plus zlib, OpenSSL 3, Opus and libsodium.
`third_party/` bundles miniaudio, QR-Code-generator, nlohmann/json, and
Discord's libdave with Cisco's mlspp.

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

To cross-compile from Linux, see `tools/package-windows.sh`.
`tools/mock_discord.py` is a small fake Discord server (REST, gateway, QR login,
CDN and voice) for trying the client out without an account:

```
python3 tools/mock_discord.py
KESTREL_API=http://127.0.0.1:8765/api/v9 KESTREL_WEB=http://127.0.0.1:8765 \
KESTREL_GATEWAY="ws://127.0.0.1:8765/gateway?encoding=json&v=9&compress=zlib-stream" \
KESTREL_REMOTE_AUTH=ws://127.0.0.1:8765/remote-auth KESTREL_CDN=http://127.0.0.1:8765/cdn \
KESTREL_MEDIA=http://127.0.0.1:8765/media KESTREL_VOICE_SCHEME=ws ./build/kestrel --token mock-token
```

## License

GPL-3.0. The voice end-to-end encryption session handling is adapted from
[Abaddon](https://github.com/uowuo/abaddon) (GPL-3.0). libdave is MIT, mlspp
is BSD-2-Clause, miniaudio is public domain / MIT-0, QR-Code-generator is
MIT, and nlohmann/json is MIT.
