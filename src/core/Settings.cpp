#include "Settings.h"

#include <QDir>
#include <QStandardPaths>

namespace kestrel {

Settings::Settings(QObject *parent)
    : QObject(parent) {
    QDir().mkpath(dataDir());
    m_settings = new QSettings(dataDir() + "/kestrel.ini", QSettings::IniFormat, this);
}

QString Settings::dataDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QVariant Settings::value(const QString &key, const QVariant &fallback) const {
    return m_settings->value(key, fallback);
}

void Settings::set(const QString &key, const QVariant &v) {
    if (m_settings->value(key) == v) return;
    m_settings->setValue(key, v);
    emit changed();
}

double Settings::userVolume(const QString &userId) const {
    return value("volumes/" + userId, 1.0).toDouble();
}

void Settings::setUserVolume(const QString &userId, double v) {
    set("volumes/" + userId, v);
}

bool Settings::categoryCollapsed(const QString &id) const {
    return value("collapsed/" + id, false).toBool();
}

void Settings::setCategoryCollapsed(const QString &id, bool v) {
    set("collapsed/" + id, v);
}

} // namespace kestrel
