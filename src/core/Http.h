#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <functional>

namespace kestrel {

// Where to talk to. Overridable with environment variables so the client can
// be exercised against a local mock server during development.
struct Endpoints {
    QString api = "https://discord.com/api/v9";
    QString web = "https://discord.com";
    QString gateway = "wss://gateway.discord.gg/?encoding=json&v=9&compress=zlib-stream";
    QString remoteAuth = "wss://remote-auth-gateway.discord.gg/?v=2";
    QString cdn = "https://cdn.discordapp.com";
    QString media = "https://media.discordapp.net";

    static Endpoints fromEnvironment();
    // Rewrites a real Discord CDN url for the mock server when overridden.
    QString mapCdn(QString url) const;
};

struct HttpResponse {
    int status = 0;
    QJsonDocument json;
    QByteArray body;
    QString error;
    bool ok() const { return status >= 200 && status < 300; }
};

using HttpCallback = std::function<void(const HttpResponse &)>;

// REST client that presents itself the way the official web client does
// (same user agent, super-properties header, cookies, locale headers).
class Http : public QObject {
    Q_OBJECT

public:
    explicit Http(const Endpoints &ep, QObject *parent = nullptr);

    static QString userAgent();
    static QString browserVersion();

    void setToken(const QString &token) { m_token = token; }
    void setBuildNumber(int n);
    int buildNumber() const { return m_buildNumber; }
    QByteArray superPropertiesJson() const;
    QJsonObject identifyProperties() const;

    // Loads discord.com/app once to pick up Cloudflare cookies and the current
    // web client build number, then calls done().
    void bootstrap(std::function<void()> done);

    void get(const QString &path, HttpCallback cb = {});
    void post(const QString &path, const QJsonObject &body, HttpCallback cb = {});
    void postEmpty(const QString &path, HttpCallback cb = {});
    void patch(const QString &path, const QJsonObject &body, HttpCallback cb = {});
    void put(const QString &path, HttpCallback cb = {});
    void del(const QString &path, HttpCallback cb = {});
    void postMultipart(const QString &path, const QJsonObject &payload, const QStringList &files, HttpCallback cb = {});

    QNetworkAccessManager *nam() { return m_nam; }

private:
    void send(const QByteArray &verb, const QString &path, const QByteArray &body, const QByteArray &contentType,
              HttpCallback cb, int attempt = 0);
    QNetworkRequest makeRequest(const QString &path) const;
    void fetchBuildNumber(const QByteArray &appPage, std::function<void()> done);

    Endpoints m_ep;
    QNetworkAccessManager *m_nam;
    QString m_token;
    int m_buildNumber = 0;
    QString m_launchId;
    QString m_heartbeatSessionId;
};

} // namespace kestrel
