#include "Protobuf.h"

#include <QByteArray>
#include <optional>

namespace kestrel {

namespace {

// Minimal protobuf wire-format reader.
struct Reader {
    const uint8_t *p;
    const uint8_t *end;
    bool bad = false;

    bool atEnd() const { return p >= end || bad; }

    uint64_t varint() {
        uint64_t result = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (p >= end) { bad = true; return 0; }
            const uint8_t b = *p++;
            result |= static_cast<uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) return result;
        }
        bad = true;
        return 0;
    }

    uint64_t fixed64() {
        if (end - p < 8) { bad = true; return 0; }
        uint64_t v = 0;
        for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
        p += 8;
        return v;
    }

    Reader sub() {
        const uint64_t len = varint();
        if (bad || static_cast<uint64_t>(end - p) < len) { bad = true; return { p, p, true }; }
        Reader r { p, p + len };
        p += len;
        return r;
    }

    void skip(int wireType) {
        switch (wireType) {
            case 0: varint(); break;
            case 1: if (end - p < 8) bad = true; else p += 8; break;
            case 2: sub(); break;
            case 5: if (end - p < 4) bad = true; else p += 4; break;
            default: bad = true;
        }
    }
};

// google.protobuf wrapper types: field 1 holds the value
std::optional<uint64_t> wrappedVarint(Reader r) {
    while (!r.atEnd()) {
        const uint64_t tag = r.varint();
        if ((tag >> 3) == 1 && (tag & 7) == 0) return r.varint();
        r.skip(static_cast<int>(tag & 7));
    }
    return std::nullopt;
}

QString wrappedString(Reader r) {
    while (!r.atEnd()) {
        const uint64_t tag = r.varint();
        if ((tag >> 3) == 1 && (tag & 7) == 2) {
            auto s = r.sub();
            return QString::fromUtf8(reinterpret_cast<const char *>(s.p), static_cast<int>(s.end - s.p));
        }
        r.skip(static_cast<int>(tag & 7));
    }
    return {};
}

GuildFolder parseFolder(Reader r) {
    GuildFolder f;
    while (!r.atEnd()) {
        const uint64_t tag = r.varint();
        const int field = static_cast<int>(tag >> 3), wt = static_cast<int>(tag & 7);
        if (field == 1 && wt == 2) { // packed fixed64 guild ids
            auto ids = r.sub();
            while (!ids.atEnd()) f.guildIds.append(ids.fixed64());
        } else if (field == 1 && wt == 1) {
            f.guildIds.append(r.fixed64());
        } else if (field == 2 && wt == 2) {
            f.id = static_cast<qint64>(wrappedVarint(r.sub()).value_or(0));
        } else if (field == 3 && wt == 2) {
            f.name = wrappedString(r.sub());
        } else if (field == 4 && wt == 2) {
            const auto c = wrappedVarint(r.sub());
            if (c) f.color = static_cast<qint64>(*c);
        } else {
            r.skip(wt);
        }
    }
    return f;
}

} // namespace

QVector<GuildFolder> decodeGuildFolders(const QByteArray &base64) {
    const QByteArray raw = QByteArray::fromBase64(base64);
    Reader top { reinterpret_cast<const uint8_t *>(raw.constData()), reinterpret_cast<const uint8_t *>(raw.constData()) + raw.size() };
    QVector<GuildFolder> folders;
    while (!top.atEnd()) {
        const uint64_t tag = top.varint();
        const int field = static_cast<int>(tag >> 3), wt = static_cast<int>(tag & 7);
        if (field == 14 && wt == 2) { // guild_folders
            auto gf = top.sub();
            while (!gf.atEnd()) {
                const uint64_t t = gf.varint();
                if ((t >> 3) == 1 && (t & 7) == 2) folders.append(parseFolder(gf.sub()));
                else gf.skip(static_cast<int>(t & 7));
            }
        } else {
            top.skip(wt);
        }
    }
    if (top.bad) return {};
    return folders;
}

} // namespace kestrel
