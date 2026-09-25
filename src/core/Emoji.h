#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace kestrel {

struct EmojiEntry {
    QString chars;
    QStringList names; // shortcodes without colons, first is the primary one
};

struct EmojiGroup {
    QString name;
    QVector<EmojiEntry> emojis;
};

// Unicode emoji table (generated from Unicode's emoji-test.txt), loaded on
// first use from the app resources.
class EmojiIndex {
public:
    static const EmojiIndex &instance();

    const QVector<EmojiGroup> &groups() const { return m_groups; }
    QString byName(const QString &shortcode) const { return m_byName.value(shortcode); }
    QVector<const EmojiEntry *> search(const QString &query, int limit) const;
    // Replaces :shortcode: with the emoji character, like the official client does on send.
    QString replaceShortcodes(const QString &text) const;

private:
    EmojiIndex();
    QVector<EmojiGroup> m_groups;
    QHash<QString, QString> m_byName;
};

} // namespace kestrel
