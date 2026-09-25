#pragma once

#include "core/Http.h"
#include "core/Types.h"

#include <QGuiApplication>
#include <QScreen>
#include <QUrl>

namespace kestrel::urls {

inline Endpoints &endpoints() {
    static Endpoints ep = Endpoints::fromEnvironment();
    return ep;
}

inline qreal dpr() {
    static qreal v = qGuiApp && qGuiApp->primaryScreen() ? qGuiApp->primaryScreen()->devicePixelRatio() : 1.0;
    return v;
}

// Discord's CDN serves a limited set of sizes (powers of two).
inline int cdnSize(int px) {
    int s = 16;
    while (s < px && s < 4096) s *= 2;
    return s;
}

// Wraps a remote url into our image provider url, decoded at w x h (logical px).
inline QString provider(const QString &url, int w, int h, bool crop) {
    if (url.isEmpty()) return {};
    const int pw = qRound(w * dpr()), ph = qRound(h * dpr());
    return QString("image://remote/%1x%2/%3/%4").arg(pw).arg(ph).arg(crop ? "c" : "n")
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(endpoints().mapCdn(url))));
}

inline QString defaultAvatar(const User &u) {
    int index = 0;
    if (u.discriminator.isEmpty() || u.discriminator == "0") index = static_cast<int>((u.id >> 22) % 6);
    else index = u.discriminator.toInt() % 5;
    return QString("https://cdn.discordapp.com/embed/avatars/%1.png").arg(index);
}

inline QString avatar(const User &u, int px, bool animated = false) {
    if (u.avatar.isEmpty()) return provider(defaultAvatar(u), px, px, true);
    const bool gif = animated && u.avatar.startsWith("a_");
    const QString url = QString("https://cdn.discordapp.com/avatars/%1/%2.%3?size=%4")
                            .arg(u.id).arg(u.avatar, gif ? "gif" : "png").arg(cdnSize(qRound(px * dpr())));
    return provider(url, px, px, true);
}

inline QString guildAvatar(Snowflake guildId, const User &u, const QString &hash, int px) {
    if (hash.isEmpty()) return avatar(u, px);
    const QString url = QString("https://cdn.discordapp.com/guilds/%1/users/%2/avatars/%3.png?size=%4")
                            .arg(guildId).arg(u.id).arg(hash).arg(cdnSize(qRound(px * dpr())));
    return provider(url, px, px, true);
}

inline QString guildIcon(const Guild &g, int px) {
    if (g.icon.isEmpty()) return {};
    return provider(QString("https://cdn.discordapp.com/icons/%1/%2.png?size=%3").arg(g.id).arg(g.icon).arg(cdnSize(qRound(px * dpr()))),
                    px, px, true);
}

inline QString channelIcon(const Channel &c, int px) {
    if (c.icon.isEmpty()) return {};
    return provider(QString("https://cdn.discordapp.com/channel-icons/%1/%2.png?size=%3").arg(c.id).arg(c.icon).arg(cdnSize(qRound(px * dpr()))),
                    px, px, true);
}

// Ask Discord's media proxy for a pre-shrunk version of an attachment/embed image.
inline QString media(const QString &proxyUrl, const QString &url, int w, int h) {
    QString base = proxyUrl.isEmpty() ? url : proxyUrl;
    if (base.isEmpty()) return {};
    const int pw = qRound(w * dpr()), ph = qRound(h * dpr());
    if (base.contains("media.discordapp.net") || base.contains("images-ext-") || proxyUrl == base) {
        base += (base.contains('?') ? "&" : "?") + QString("width=%1&height=%2").arg(pw).arg(ph);
    }
    return provider(base, w, h, false);
}

inline QString initials(const QString &name) {
    QString out;
    for (const auto &word : name.split(' ', Qt::SkipEmptyParts)) {
        for (QChar c : word) {
            if (c.isLetterOrNumber()) { out += c; break; }
        }
        if (out.size() >= 3) break;
    }
    return out.isEmpty() ? name.left(2) : out;
}

} // namespace kestrel::urls
