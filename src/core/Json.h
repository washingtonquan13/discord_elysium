#pragma once
// Tolerant JSON accessors. Discord changes field types without notice
// (numbers become null, ids arrive as numbers or strings), so nothing here
// ever throws: a missing or mistyped field simply yields the fallback.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <cstdint>

namespace kestrel::json {

using Snowflake = quint64;

inline QString str(const QJsonObject &o, const char *key, const QString &fallback = {}) {
    const auto v = o.value(QLatin1String(key));
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(static_cast<qint64>(v.toDouble()));
    return fallback;
}

inline qint64 i64(const QJsonObject &o, const char *key, qint64 fallback = 0) {
    const auto v = o.value(QLatin1String(key));
    if (v.isDouble()) return static_cast<qint64>(v.toDouble());
    if (v.isString()) {
        bool ok = false;
        const auto n = v.toString().toLongLong(&ok);
        return ok ? n : fallback;
    }
    if (v.isBool()) return v.toBool() ? 1 : 0;
    return fallback;
}

inline int i32(const QJsonObject &o, const char *key, int fallback = 0) {
    return static_cast<int>(i64(o, key, fallback));
}

inline bool boolean(const QJsonObject &o, const char *key, bool fallback = false) {
    const auto v = o.value(QLatin1String(key));
    if (v.isBool()) return v.toBool();
    if (v.isDouble()) return v.toDouble() != 0.0;
    return fallback;
}

inline Snowflake toId(const QJsonValue &v) {
    if (v.isString()) return v.toString().toULongLong();
    if (v.isDouble()) return static_cast<Snowflake>(v.toDouble());
    return 0;
}

inline Snowflake id(const QJsonObject &o, const char *key = "id") {
    return toId(o.value(QLatin1String(key)));
}

// Permission bitfields are sent as decimal strings (they exceed 2^53).
inline quint64 bits(const QJsonObject &o, const char *key) {
    const auto v = o.value(QLatin1String(key));
    if (v.isString()) return v.toString().toULongLong();
    if (v.isDouble()) return static_cast<quint64>(v.toDouble());
    return 0;
}

inline QJsonObject obj(const QJsonObject &o, const char *key) {
    const auto v = o.value(QLatin1String(key));
    return v.isObject() ? v.toObject() : QJsonObject {};
}

inline QJsonArray arr(const QJsonObject &o, const char *key) {
    const auto v = o.value(QLatin1String(key));
    return v.isArray() ? v.toArray() : QJsonArray {};
}

inline bool has(const QJsonObject &o, const char *key) {
    return o.contains(QLatin1String(key));
}

inline bool isNull(const QJsonObject &o, const char *key) {
    const auto v = o.value(QLatin1String(key));
    return v.isNull() || v.isUndefined();
}

inline QByteArray dump(const QJsonObject &o) {
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

inline QString idStr(Snowflake s) {
    return QString::number(s);
}

} // namespace kestrel::json
