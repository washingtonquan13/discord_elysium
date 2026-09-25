#include "Markdown.h"

#include <QColor>
#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>
#include <QUrl>

namespace kestrel {

namespace {

QString esc(const QString &s) {
    QString out;
    out.reserve(s.size() + 16);
    for (QChar c : s) {
        switch (c.unicode()) {
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

QString imageUrl(const QString &url, int size) {
    return QString("image://remote/%1x%1/n/%2").arg(size).arg(QString::fromLatin1(QUrl::toPercentEncoding(url)));
}

QString relativeTime(const QDateTime &t) {
    const qint64 secs = QDateTime::currentDateTime().secsTo(t);
    const qint64 a = std::llabs(secs);
    QString unit;
    qint64 n;
    if (a < 60) { n = a; unit = "second"; }
    else if (a < 3600) { n = a / 60; unit = "minute"; }
    else if (a < 86400) { n = a / 3600; unit = "hour"; }
    else if (a < 86400 * 30) { n = a / 86400; unit = "day"; }
    else if (a < 86400 * 365) { n = a / (86400 * 30); unit = "month"; }
    else { n = a / (86400 * 365); unit = "year"; }
    const QString s = QString("%1 %2%3").arg(n).arg(unit).arg(n == 1 ? "" : "s");
    return secs >= 0 ? "in " + s : s + " ago";
}

QString formatTimestamp(qint64 seconds, const QString &style) {
    const auto t = QDateTime::fromSecsSinceEpoch(seconds).toLocalTime();
    const QLocale loc;
    if (style == "t") return loc.toString(t.time(), QLocale::ShortFormat);
    if (style == "T") return loc.toString(t.time(), QLocale::LongFormat);
    if (style == "d") return loc.toString(t.date(), QLocale::ShortFormat);
    if (style == "D") return loc.toString(t.date(), "MMMM d, yyyy");
    if (style == "F") return loc.toString(t, "dddd, MMMM d, yyyy h:mm AP");
    if (style == "R") return relativeTime(t);
    return loc.toString(t, "MMMM d, yyyy h:mm AP");
}

class InlineParser {
public:
    InlineParser(const MarkdownContext &ctx, bool plain)
        : m_ctx(ctx)
        , m_plain(plain) {}

    QString parse(const QString &s, int depth = 0) {
        QString out;
        int i = 0;
        const int n = s.size();
        while (i < n) {
            const QChar c = s[i];

            // backslash escapes
            if (c == '\\' && i + 1 < n && QString("\\*_~`|<>[]()#-:").contains(s[i + 1])) {
                out += m_plain ? QString(s[i + 1]) : esc(QString(s[i + 1]));
                i += 2;
                continue;
            }

            // inline code
            if (c == '`') {
                int ticks = 1;
                while (i + ticks < n && s[i + ticks] == '`' && ticks < 2) ticks++;
                const QString fence(ticks, '`');
                const int close = s.indexOf(fence, i + ticks);
                if (close > i + ticks) {
                    const QString code = s.mid(i + ticks, close - i - ticks);
                    out += m_plain ? code
                                   : QString("<code style=\"background-color:%1;font-family:Consolas,'Cascadia Mono',monospace\">&nbsp;%2&nbsp;</code>")
                                         .arg(m_ctx.codeBg, esc(code).replace(' ', "&nbsp;"));
                    i = close + ticks;
                    continue;
                }
            }

            if (c == '<') {
                if (int used = angle(s, i, out); used > 0) {
                    i += used;
                    continue;
                }
            }

            // masked links [text](https://...)
            if (c == '[' && depth < 4) {
                static const QRegularExpression re(R"(\[([^\]\n]{1,256})\]\(<?(https?://[^\s)>]+)>?\))");
                const auto m = re.match(s, i, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
                if (m.hasMatch()) {
                    out += link(m.captured(2), parse(m.captured(1), depth + 1));
                    i += m.capturedLength(0);
                    continue;
                }
            }

            // bare links
            if ((c == 'h' || c == 'H') && (s.mid(i, 7).compare("http://", Qt::CaseInsensitive) == 0 || s.mid(i, 8).compare("https://", Qt::CaseInsensitive) == 0)) {
                static const QRegularExpression re(R"(https?://[^\s<]+[^\s<.,:;"')\]!?])");
                const auto m = re.match(s, i, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
                if (m.hasMatch()) {
                    out += link(m.captured(0), esc(m.captured(0)));
                    i += m.capturedLength(0);
                    continue;
                }
            }

            // emphasis family
            if (depth < 8) {
                if (int used = emphasis(s, i, "***", "<b><i>", "</i></b>", out, depth)) { i += used; continue; }
                if (int used = emphasis(s, i, "**", "<b>", "</b>", out, depth)) { i += used; continue; }
                if (int used = emphasis(s, i, "__", "<u>", "</u>", out, depth)) { i += used; continue; }
                if (int used = emphasis(s, i, "~~", "<s>", "</s>", out, depth)) { i += used; continue; }
                if (int used = emphasis(s, i, "||", spoilerOpen(), "</span>", out, depth)) { i += used; continue; }
                if (c == '*' && (i + 1 < n && !s[i + 1].isSpace())) {
                    if (int used = emphasis(s, i, "*", "<i>", "</i>", out, depth)) { i += used; continue; }
                }
                if (c == '_' && (i == 0 || !s[i - 1].isLetterOrNumber())) {
                    const int close = s.indexOf('_', i + 1);
                    if (close > i + 1 && (close + 1 >= n || !s[close + 1].isLetterOrNumber())) {
                        out += m_plain ? parse(s.mid(i + 1, close - i - 1), depth + 1)
                                       : "<i>" + parse(s.mid(i + 1, close - i - 1), depth + 1) + "</i>";
                        i = close + 1;
                        continue;
                    }
                }
            }

            // @everyone / @here
            if (c == '@' && (s.mid(i, 9) == "@everyone" || s.mid(i, 5) == "@here")) {
                const QString word = s.mid(i, 9) == "@everyone" ? "@everyone" : "@here";
                out += mention(word);
                i += word.size();
                continue;
            }

            if (c == '\n') {
                out += m_plain ? "\n" : "<br>";
            } else if (c == ' ' && i + 1 < n && s[i + 1] == ' ' && !m_plain) {
                out += "&nbsp;";
            } else {
                out += m_plain ? QString(c) : esc(QString(c));
            }
            i++;
        }
        return out;
    }

private:
    QString spoilerOpen() const {
        return "<span style=\"background-color:#1e1f22;color:#b5bac1\">";
    }

    QString link(const QString &url, const QString &label) const {
        if (m_plain) return label;
        return QString("<a href=\"%1\" style=\"color:%2;text-decoration:none\">%3</a>").arg(esc(url), m_ctx.linkColor, label);
    }

    QString mention(const QString &label, const QString &href = {}, const QString &fg = {}, const QString &bg = {}) const {
        if (m_plain) return label;
        const QString span = QString("<span style=\"background-color:%1;color:%2;font-weight:500\">&nbsp;%3&nbsp;</span>")
                                 .arg(bg.isEmpty() ? m_ctx.mentionBg : bg, fg.isEmpty() ? m_ctx.mentionFg : fg, esc(label));
        if (href.isEmpty()) return span;
        return QString("<a href=\"%1\" style=\"text-decoration:none\">%2</a>").arg(href, span);
    }

    int emphasis(const QString &s, int i, const QString &delim, const QString &open, const QString &close, QString &out, int depth) {
        if (!s.mid(i).startsWith(delim)) return 0;
        const int start = i + delim.size();
        int end = s.indexOf(delim, start);
        // don't let "**" close a single "*" run etc.
        while (end > start && delim.size() == 1 && end + 1 < s.size() && s[end + 1] == delim[0]) {
            end = s.indexOf(delim, end + 2);
        }
        if (end <= start) return 0;
        const QString inner = parse(s.mid(start, end - start), depth + 1);
        out += m_plain ? inner : open + inner + close;
        return end + delim.size() - i;
    }

    // <@id> <@!id> <#id> <@&id> <:name:id> <a:name:id> <t:123:R> <https://...>
    int angle(const QString &s, int i, QString &out) {
        static const QRegularExpression re(
            R"(<(?:(@!?)(\d{15,21})|(#)(\d{15,21})|(@&)(\d{15,21})|(a?):(\w{1,32}):(\d{15,21})|t:(-?\d{1,13})(?::([tTdDfFR]))?|(https?://[^\s>]+))>)");
        const auto m = re.match(s, i, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
        if (!m.hasMatch()) return 0;

        if (!m.captured(2).isEmpty()) {
            const auto id = m.captured(2).toULongLong();
            const QString name = m_ctx.userName ? m_ctx.userName(id) : QString();
            out += mention("@" + (name.isEmpty() ? QString("unknown-user") : name), "kestrel://user/" + m.captured(2));
        } else if (!m.captured(4).isEmpty()) {
            const auto id = m.captured(4).toULongLong();
            const QString name = m_ctx.channelName ? m_ctx.channelName(id) : QString();
            out += mention("#" + (name.isEmpty() ? QString("unknown") : name), "kestrel://channel/" + m.captured(4));
        } else if (!m.captured(6).isEmpty()) {
            const auto id = m.captured(6).toULongLong();
            QPair<QString, quint32> role { {}, 0 };
            if (m_ctx.roleName) role = m_ctx.roleName(id);
            if (role.second != 0) {
                const QColor col = QColor::fromRgb(role.second);
                QColor bg = col;
                bg.setAlphaF(0.2);
                // Qt rich text has no alpha backgrounds; blend over the chat background.
                const QColor base("#313338");
                const QColor mixed = QColor::fromRgbF(base.redF() * 0.8 + col.redF() * 0.2, base.greenF() * 0.8 + col.greenF() * 0.2,
                                                      base.blueF() * 0.8 + col.blueF() * 0.2);
                out += mention("@" + (role.first.isEmpty() ? "deleted-role" : role.first), {}, col.name(), mixed.name());
            } else {
                out += mention("@" + (role.first.isEmpty() ? "deleted-role" : role.first));
            }
        } else if (!m.captured(9).isEmpty()) {
            const bool animated = m.captured(7) == "a";
            const QString name = m.captured(8);
            if (m_plain) {
                out += ":" + name + ":";
            } else {
                const int px = m_ctx.jumbo ? 48 : m_ctx.emojiSize;
                QString url = QString("https://cdn.discordapp.com/emojis/%1.%2?size=%3&quality=lossless")
                                  .arg(m.captured(9), animated ? "gif" : "png").arg(px * 2);
                if (m_ctx.mapUrl) url = m_ctx.mapUrl(url);
                out += QString("<img src=\"%1\" width=\"%2\" height=\"%2\" align=\"middle\" alt=\":%3:\">")
                           .arg(imageUrl(url, px * 2)).arg(px).arg(esc(name));
            }
        } else if (!m.captured(10).isEmpty()) {
            const QString text = formatTimestamp(m.captured(10).toLongLong(), m.captured(11));
            out += m_plain ? text
                           : QString("<span style=\"background-color:#3f4147\">&nbsp;%1&nbsp;</span>").arg(esc(text));
        } else if (!m.captured(12).isEmpty()) {
            out += link(m.captured(12), esc(m.captured(12)));
        }
        return m.capturedLength(0);
    }

    const MarkdownContext &m_ctx;
    bool m_plain;
};

QString codeBlock(const QString &code, const MarkdownContext &ctx) {
    QString c = code;
    if (c.endsWith('\n')) c.chop(1);
    return QString("<table width=\"100%\" cellpadding=\"8\" cellspacing=\"0\" bgcolor=\"%1\" style=\"margin-top:4px;margin-bottom:4px\">"
                   "<tr><td><pre style=\"font-family:Consolas,'Cascadia Mono',monospace;margin:0\">%2</pre></td></tr></table>")
        .arg(ctx.codeBg, esc(c));
}

QString quoteBlock(const QString &innerHtml) {
    return QString("<table cellspacing=\"0\" cellpadding=\"0\" style=\"margin-top:2px;margin-bottom:2px\"><tr>"
                   "<td width=\"4\" bgcolor=\"#4e5058\"></td><td width=\"10\"></td><td>%1</td></tr></table>")
        .arg(innerHtml);
}

// Handles block-level syntax for a run of lines without code fences.
QString blocks(const QString &text, const MarkdownContext &ctx) {
    InlineParser inl(ctx, false);
    const QStringList lines = text.split('\n');
    QString out;
    QStringList quote;
    QStringList para;

    auto flushPara = [&]() {
        if (para.isEmpty()) return;
        if (!out.isEmpty() && !out.endsWith("</table>") && !out.endsWith("</div>")) out += "<br>";
        out += inl.parse(para.join('\n'));
        para.clear();
    };
    auto flushQuote = [&]() {
        if (quote.isEmpty()) return;
        flushPara();
        out += quoteBlock(blocks(quote.join('\n'), ctx));
        quote.clear();
    };

    for (int li = 0; li < lines.size(); li++) {
        const QString &line = lines[li];
        if (line.startsWith(">>> ")) {
            flushPara();
            QStringList rest = lines.mid(li);
            rest[0] = rest[0].mid(4);
            quote += rest;
            break;
        }
        if (line.startsWith("> ") || line == ">") {
            flushPara();
            quote.append(line.mid(2));
            continue;
        }
        flushQuote();

        static const QRegularExpression header(R"(^(#{1,3}) (.+)$)");
        static const QRegularExpression subtext(R"(^-# (.+)$)");
        static const QRegularExpression list(R"(^(\s*)[-*] (.+)$)");
        static const QRegularExpression olist(R"(^(\s*)(\d{1,3})\. (.+)$)");
        if (auto m = header.match(line); m.hasMatch()) {
            flushPara();
            const int level = m.captured(1).size();
            const int px = level == 1 ? 22 : level == 2 ? 19 : 16;
            out += QString("<div style=\"font-size:%1px;font-weight:700;margin-top:6px\">%2</div>").arg(px).arg(inl.parse(m.captured(2)));
        } else if (auto m = subtext.match(line); m.hasMatch()) {
            flushPara();
            out += QString("<div style=\"font-size:11px;color:#949ba4\">%1</div>").arg(inl.parse(m.captured(1)));
        } else if (auto m = list.match(line); m.hasMatch()) {
            flushPara();
            const int indent = 8 + m.captured(1).size() * 8;
            out += QString("<div style=\"margin-left:%1px\">&bull;&nbsp;%2</div>").arg(indent).arg(inl.parse(m.captured(2)));
        } else if (auto m = olist.match(line); m.hasMatch()) {
            flushPara();
            const int indent = 8 + m.captured(1).size() * 8;
            out += QString("<div style=\"margin-left:%1px\">%2.&nbsp;%3</div>").arg(indent).arg(m.captured(2), inl.parse(m.captured(3)));
        } else {
            para.append(line);
        }
    }
    flushQuote();
    flushPara();
    return out;
}

} // namespace

QString markdownToHtml(const QString &source, const MarkdownContext &ctx) {
    QString out;
    int pos = 0;
    static const QRegularExpression fence(R"(```(?:([A-Za-z0-9_+#.-]{1,20})\n)?([\s\S]*?)```)");
    auto it = fence.globalMatch(source);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString before = source.mid(pos, m.capturedStart(0) - pos);
        if (!before.trimmed().isEmpty()) out += blocks(before.endsWith('\n') ? before.chopped(1) : before, ctx);
        out += codeBlock(m.captured(2), ctx);
        pos = m.capturedEnd(0);
    }
    QString rest = source.mid(pos);
    if (rest.startsWith('\n')) rest.remove(0, 1);
    if (!rest.isEmpty()) out += blocks(rest, ctx);
    return out;
}

QString markdownToPlain(const QString &source, const MarkdownContext &ctx) {
    InlineParser p(ctx, true);
    return p.parse(source);
}

bool isEmojiOnly(const QString &source) {
    const QString s = source.trimmed();
    if (s.isEmpty() || s.size() > 200) return false;
    static const QRegularExpression custom(R"(<a?:\w{1,32}:\d{15,21}>)");
    QString rest = s;
    rest.remove(custom);
    int count = s.count(custom);
    const auto ucs = rest.toUcs4();
    for (char32_t cp : ucs) {
        if (cp == ' ' || cp == 0xFE0F || cp == 0x200D || (cp >= 0x1F3FB && cp <= 0x1F3FF)) continue;
        const bool emoji = (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF)
            || (cp >= 0x1F1E6 && cp <= 0x1F1FF) || cp == 0x2764 || (cp >= 0x2190 && cp <= 0x21FF) || (cp >= 0xE0020 && cp <= 0xE007F);
        if (!emoji) return false;
        count++;
    }
    return count > 0 && count <= 30;
}

} // namespace kestrel
