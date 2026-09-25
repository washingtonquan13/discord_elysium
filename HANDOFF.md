# Handoff: Discord Elysium (Kestrel client)

This handoff is for a Claude Code session picking up this project. Read it fully
before changing anything.

## What this is

**Kestrel** is a lightweight, non-Electron Discord client for Windows, written in
C++20, Qt 6 (Qt Quick/QML) and CMake. The owner wanted Discord with far less RAM
and better image handling than Abaddon (a GTK client), and with a Discord-like
look. The repo is `washingtonquan13/discord_elysium`. The app is still called
"Kestrel" in code; renaming it to "Elysium" has been suggested but not done.

It's a third-party client. Discord's terms discourage them, and accounts can get
flagged by the spam filter. The client imitates the official **web** client
(Chrome user agent, `X-Super-Properties`, a build number scraped from
discord.com, Cloudflare cookies, identify with capabilities 4605). Keep it that
way: don't add endpoints the web client doesn't use.

## Status

- The whole client is written and works end to end against a local mock server
  (`tools/mock_discord.py`).
- A Windows build was produced by cross-compiling from Linux with mingw-w64,
  against Qt 6.4.2 built from source. It ran under Wine against the mock.
- **Not yet tested against real Discord or with real audio devices.** The first
  real-world run is the next milestone. Expect small protocol surprises,
  especially in voice.
- The user's log file on Windows is `%APPDATA%\Kestrel\Kestrel\kestrel.log`.
  It gets every qInfo/qWarning line and never the token. Ask for it when
  debugging.

## Features implemented

- **Login:** QR code (remote-auth v2, RSA-OAEP key exchange) or token. The token
  is stored with Windows DPAPI (`TokenStore`).
- **Gateway:** zlib-stream, heartbeats, resume/reconnect with backoff, and
  tolerant JSON parsing that never throws on a wrong-typed field (`core/Json.h`).
  That tolerance is the whole reason Abaddon broke.
- **Servers:** servers and folders (decoded from `user_settings_proto` in
  `core/Protobuf.cpp`), categories, permission-filtered channels, unread and
  mention badges, muted servers and channels, DMs and group DMs.
- **Messages:** a Discord markdown to Qt rich-text converter (`core/Markdown.cpp`),
  embeds, attachments, replies, edit (↑ key), delete (with confirm), reactions,
  typing, jump to message, uploads (button or drag-drop).
- **Images:** `ui/ImageProvider.cpp` decodes at display size on a thread pool,
  keeps a 256 MB disk cache and a 24 MB in-memory LRU, and requests pre-shrunk
  images from Discord's media proxy.
- **Member list:** built from `GUILD_MEMBER_LIST_UPDATE` via op 37 subscriptions,
  plus profile popups.
- **Input and navigation:** emoji picker (`resources/emoji.json` generated from
  Unicode emoji-test 16.0), `:emoji:`, `@user` and `#channel` autocomplete,
  quick switcher (Ctrl+K), Alt+↑/↓ to change channel, tray icon, notifications.
- **Voice:** voice gateway v8, UDP with `aead_xchacha20_poly1305_rtpsize`, Opus,
  miniaudio (WASAPI), and **DAVE end-to-end encryption via Discord's libdave**
  (session logic adapted from Abaddon, GPL-3.0). Also voice activity with a
  threshold or push-to-talk (`GetAsyncKeyState`, works in games),
  mute/deafen, per-user volume, speaking rings, and a mic test.

## Not implemented

Video, screen share and watching streams; threads and forum channels;
stickers (only the name is shown); polls; slash commands; editing
profile/server settings; joining/leaving servers.

## Layout

```
src/core/     Http, Gateway, RemoteAuth, Store (all account state), Client (facade),
              Markdown, Emoji, Permissions, Protobuf, Settings, TokenStore, Types, Json
src/voice/    VoiceClient (signalling + threading), VoiceUdp (RTP/crypto),
              AudioEngine (miniaudio + opus), DaveSession (libdave wrapper)
src/ui/       AppController (the single object QML talks to), list models,
              ImageProvider, QrProvider, Urls (CDN url helpers)
qml/          UI. Theme.qml is a singleton with all colours.
third_party/  libdave, mlspp, miniaudio, qrcodegen, nlohmann json (vendored)
tools/        mock_discord.py, package-windows.sh, qt-6.4.2-windows-fontengine.patch
```

## Important design decisions and gotchas

- Everything network-related runs on the Qt main thread. Voice runs on three
  threads: main, the miniaudio capture/playback threads, and the UDP receive
  thread.
- `g_pipeMutex` in VoiceClient.cpp guards `m_udp`/`m_dave`. Lock order is always
  `g_pipeMutex` then DaveSession's mutex. **Never join the UDP thread while
  holding `g_pipeMutex`.** Use `teardownPipe()`, which swaps the pointers out
  under the lock and destroys them afterwards; the other way deadlocks.
- A voice state for our own user whose `session_id` differs from our gateway
  session belongs to another device (for example the phone). Ignore it.
- `resume_gateway_url` has no path; `Gateway::open()` adds `/`. Resume falls back
  to a fresh identify after two failed attempts.
- `MessageModel` rows are newest first, shown in a BottomToTop ListView. It keeps
  an LRU of 6 channels, and merges REST pages with live events by id.
- `Store::handleDispatch` is the single place gateway events mutate state. Models
  listen to its signals.
- Env vars `KESTREL_API/WEB/GATEWAY/REMOTE_AUTH/CDN/MEDIA/VOICE_SCHEME` point the
  client at the mock server. `KESTREL_AUDIO_NULL=1` uses a silent audio backend.
  `KESTREL_TEST_SCRIPT=file.qml` runs a QML script against the live UI (it can
  call `app.*` and `app.screenshot(path)`).
- rcc is run with `--no-zstd`, because a host rcc built with zstd produces
  resources a target Qt without zstd can't link.
- **Qt patch:** `tools/qt-6.4.2-windows-fontengine.patch` fixes a Qt 6.4 crash in
  `QWindowsFontEngine`, where `GetOutlineTextMetrics` failing gave an
  uninitialised buffer. It was found under Wine. With a newer Qt (for example
  MSYS2's), check whether it's still needed before re-applying.

## Building

**Linux (dev/testing):** Ubuntu 24.04 with `qt6-base-dev qt6-declarative-dev
qt6-websockets-dev qt6-svg-dev libopus-dev libsodium-dev libssl-dev zlib1g-dev`.
Then:

```
cmake -B build-linux -G Ninja && ninja -C build-linux kestrel
```

**Windows natively (recommended now):** use MSYS2 UCRT64 or MINGW64 and install
the packages:

```
mingw-w64-x86_64-{toolchain,cmake,ninja,qt6-base,qt6-declarative,qt6-websockets,qt6-svg,qt6-imageformats,opus,libsodium,openssl,zlib}
```

Build with CMake as above, then run `windeployqt6 --qmldir qml build\kestrel.exe`
to gather DLLs.

**Cross-compile (what was used):** `tools/package-windows.sh` expects Qt for
Windows in `$QT`, the deps in `$PREFIX` (static zlib, opus, libsodium,
openssl 3), and a mingw toolchain file. The Qt 6.4.2 modules were built from
source because the workspace couldn't download prebuilt Qt.

## Testing

```
python3 tools/mock_discord.py          # needs aiohttp, pillow, cryptography, pynacl
# MOCK_QR_AUTOSCAN=1 simulates a phone scanning the QR code
# GET http://127.0.0.1:8765/debug/drop?code=4000 forces a gateway disconnect
./build-linux/kestrel --token mock-token     # with the KESTREL_* env vars (see README)
```

The mock echoes voice packets back as another speaker, which verifies transport
encryption both ways. Its DAVE version is 0, so **DAVE is untested against a
real peer.**

## Suggested next steps

1. First real-Discord run with the user. Fix whatever the log shows (READY field
   shapes, member list, voice).
2. Verify voice plus DAVE in a real call. One known uncertainty: when op 24
   (epoch 1) creates the DAVE session, `init()` then `onPrepareEpoch()` both
   reinitialise, which sends two key packages. Abaddon does the same thing.
3. Nice to have: threads/forums, sticker images, per-channel notification
   settings, rename to Elysium, CI (GitHub Actions on windows-latest with
   MSYS2) to produce release zips.

## Conventions

- Commit as the user. Messages end with the Co-Authored-By / Claude-Session
  trailers used in the existing history.
- Keep the UI close to Discord's layout. Colours live in `qml/Theme.qml`.
- License: GPL-3.0, because of code adapted from Abaddon.
