#include "Emoji.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

namespace kestrel {

const EmojiIndex &EmojiIndex::instance() {
    static EmojiIndex idx;
    return idx;
}

EmojiIndex::EmojiIndex() {
    QFile f(":/emoji.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto groups = QJsonDocument::fromJson(f.readAll()).array();
    for (const auto &gv : groups) {
        const auto g = gv.toArray();
        EmojiGroup group;
        group.name = g.at(0).toString();
        for (const auto &ev : g.at(1).toArray()) {
            const auto e = ev.toArray();
            EmojiEntry entry { e.at(0).toString(), e.at(1).toString().split(',', Qt::SkipEmptyParts) };
            for (const auto &n : entry.names)
                if (!m_byName.contains(n)) m_byName.insert(n, entry.chars);
            group.emojis.append(entry);
        }
        m_groups.append(group);
    }
    // a few names people type out of habit from the official client
    const std::initializer_list<std::pair<const char *, const char *>> extra {
        { "thumbsup", "👍" }, { "thumbsdown", "👎" }, { "joy", "😂" }, { "sob", "😭" }, { "heart", "❤️" }, { "fire", "🔥" },
        { "skull", "💀" }, { "eyes", "👀" }, { "pray", "🙏" }, { "tada", "🎉" }, { "smile", "😄" }, { "slight_smile", "🙂" },
        { "thinking", "🤔" }, { "100", "💯" }, { "ok_hand", "👌" }, { "wave", "👋" }, { "clap", "👏" }, { "rofl", "🤣" },
        { "white_check_mark", "✅" }, { "x", "❌" }, { "sparkles", "✨" }, { "rocket", "🚀" }, { "sweat_smile", "😅" },
    };
    for (const auto &[k, v] : extra) m_byName.insert(QString::fromUtf8(k), QString::fromUtf8(v));
}

QVector<const EmojiEntry *> EmojiIndex::search(const QString &query, int limit) const {
    // rank: exact name, then name prefix (shorter first), then substring
    struct Hit { int rank; int len; int order; const EmojiEntry *e; };
    QVector<Hit> hits;
    const QString q = query.toLower();
    int order = 0;
    for (const auto &g : m_groups) {
        for (const auto &e : g.emojis) {
            order++;
            int best = 3, len = 999;
            for (const auto &n : e.names) {
                int r = n == q ? 0 : n.startsWith(q) ? 1 : n.contains(q) ? 2 : 3;
                if (r < best || (r == best && n.size() < len)) { best = r; len = static_cast<int>(n.size()); }
            }
            if (best < 3) hits.append({ best, len, order, &e });
        }
    }
    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        if (a.len != b.len) return a.len < b.len;
        return a.order < b.order;
    });
    QVector<const EmojiEntry *> out;
    for (int i = 0; i < hits.size() && i < limit; i++) out.append(hits[i].e);
    return out;
}

QString EmojiIndex::replaceShortcodes(const QString &text) const {
    static const QRegularExpression re(R"((?<![<\w]):([a-z0-9_+-]{1,40}):(?!\d))");
    QString out;
    int last = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString e = m_byName.value(m.captured(1));
        if (e.isEmpty()) continue;
        out += text.mid(last, m.capturedStart() - last) + e;
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

} // namespace kestrel
