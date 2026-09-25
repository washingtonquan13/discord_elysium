#pragma once

#include <QObject>
#include <QSettings>

namespace kestrel {

// Persistent user preferences, stored in an ini file next to the user's
// app data (%APPDATA%/Kestrel/kestrel.ini on Windows).
class Settings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString inputDevice READ inputDevice WRITE setInputDevice NOTIFY changed)
    Q_PROPERTY(QString outputDevice READ outputDevice WRITE setOutputDevice NOTIFY changed)
    Q_PROPERTY(bool pushToTalk READ pushToTalk WRITE setPushToTalk NOTIFY changed)
    Q_PROPERTY(int pushToTalkKey READ pushToTalkKey WRITE setPushToTalkKey NOTIFY changed)
    Q_PROPERTY(QString pushToTalkKeyName READ pushToTalkKeyName WRITE setPushToTalkKeyName NOTIFY changed)
    Q_PROPERTY(double vadThreshold READ vadThreshold WRITE setVadThreshold NOTIFY changed)
    Q_PROPERTY(double inputVolume READ inputVolume WRITE setInputVolume NOTIFY changed)
    Q_PROPERTY(double outputVolume READ outputVolume WRITE setOutputVolume NOTIFY changed)
    Q_PROPERTY(bool animateImages READ animateImages WRITE setAnimateImages NOTIFY changed)
    Q_PROPERTY(bool showMemberList READ showMemberList WRITE setShowMemberList NOTIFY changed)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray NOTIFY changed)
    Q_PROPERTY(bool notifications READ notifications WRITE setNotifications NOTIFY changed)
    Q_PROPERTY(bool compactMode READ compactMode WRITE setCompactMode NOTIFY changed)
    Q_PROPERTY(double uiScale READ uiScale WRITE setUiScale NOTIFY changed)

public:
    explicit Settings(QObject *parent = nullptr);

    QString inputDevice() const { return value("voice/input").toString(); }
    void setInputDevice(const QString &v) { set("voice/input", v); }
    QString outputDevice() const { return value("voice/output").toString(); }
    void setOutputDevice(const QString &v) { set("voice/output", v); }
    bool pushToTalk() const { return value("voice/ptt", false).toBool(); }
    void setPushToTalk(bool v) { set("voice/ptt", v); }
    int pushToTalkKey() const { return value("voice/ptt_key", 0).toInt(); }
    void setPushToTalkKey(int v) { set("voice/ptt_key", v); }
    QString pushToTalkKeyName() const { return value("voice/ptt_key_name").toString(); }
    void setPushToTalkKeyName(const QString &v) { set("voice/ptt_key_name", v); }
    double vadThreshold() const { return value("voice/vad", -50.0).toDouble(); }
    void setVadThreshold(double v) { set("voice/vad", v); }
    double inputVolume() const { return value("voice/in_vol", 1.0).toDouble(); }
    void setInputVolume(double v) { set("voice/in_vol", v); }
    double outputVolume() const { return value("voice/out_vol", 1.0).toDouble(); }
    void setOutputVolume(double v) { set("voice/out_vol", v); }
    bool animateImages() const { return value("ui/animate", false).toBool(); }
    void setAnimateImages(bool v) { set("ui/animate", v); }
    bool showMemberList() const { return value("ui/members", true).toBool(); }
    void setShowMemberList(bool v) { set("ui/members", v); }
    bool minimizeToTray() const { return value("ui/tray", false).toBool(); }
    void setMinimizeToTray(bool v) { set("ui/tray", v); }
    bool notifications() const { return value("ui/notify", true).toBool(); }
    void setNotifications(bool v) { set("ui/notify", v); }
    bool compactMode() const { return value("ui/compact", false).toBool(); }
    void setCompactMode(bool v) { set("ui/compact", v); }
    double uiScale() const { return value("ui/scale", 1.0).toDouble(); }
    void setUiScale(double v) { set("ui/scale", v); }

    Q_INVOKABLE double userVolume(const QString &userId) const;
    Q_INVOKABLE void setUserVolume(const QString &userId, double v);
    Q_INVOKABLE bool categoryCollapsed(const QString &id) const;
    Q_INVOKABLE void setCategoryCollapsed(const QString &id, bool v);

    QVariant value(const QString &key, const QVariant &fallback = {}) const;
    void set(const QString &key, const QVariant &v);
    QString dataDir() const;

signals:
    void changed();

private:
    QSettings *m_settings;
};

} // namespace kestrel
