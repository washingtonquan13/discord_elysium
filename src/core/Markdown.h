#pragma once

#include "Types.h"

#include <QString>
#include <functional>

namespace kestrel {

struct MarkdownContext {
    std::function<QString(Snowflake)> userName;      // <@id>
    std::function<QString(Snowflake)> channelName;   // <#id>
    std::function<QPair<QString, quint32>(Snowflake)> roleName; // <@&id> -> name, color
    std::function<QString(const QString &)> mapUrl;  // CDN rewrite (for testing)
    int emojiSize = 22;
    bool jumbo = false;
    QString linkColor = "#00a8fc";
    QString mentionBg = "#3c4270";
    QString mentionFg = "#c9cdfb";
    QString codeBg = "#232428";
};

// Converts Discord-flavoured markdown into the HTML subset Qt's rich text
// engine understands. Output is always escaped/safe.
QString markdownToHtml(const QString &source, const MarkdownContext &ctx);

// True when the message consists only of emoji (rendered larger, like Discord).
bool isEmojiOnly(const QString &source);

// Plain-text rendering for previews and notifications.
QString markdownToPlain(const QString &source, const MarkdownContext &ctx);

} // namespace kestrel
