#include "Http.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QLocale>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace kestrel {

Endpoints Endpoints::fromEnvironment() {
    Endpoints ep;
    auto env = [](const char *name, QString &target) {
        const auto v = qEnvironmentVariable(name);
        if (!v.isEmpty()) target = v;
    };
    env("KESTREL_API", ep.api);
    env("KESTREL_WEB", ep.web);
    env("KESTREL_GATEWAY", ep.gateway);
    env("KESTREL_REMOTE_AUTH", ep.remoteAuth);
    env("KESTREL_CDN", ep.cdn);
    env("KESTREL_MEDIA", ep.media);
    return ep;
}

QString Endpoints::mapCdn(QString url) const {
    if (cdn != "https://cdn.discordapp.com") url.replace("https://cdn.discordapp.com", cdn);
    if (media != "https://media.discordapp.net") url.replace("https://media.discordapp.net", media);
    return url;
}

// A current Chrome on Windows. Chrome's reduced user agent only carries the
// major version, which is what the web client reports as well.
static constexpr const char *kChromeMajor = "145";

QString Http::browserVersion() {
    return QString("%1.0.0.0").arg(kChromeMajor);
}

QString Http::userAgent() {
    return QString("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/%1 Safari/537.36")
        .arg(browserVersion());
}

Http::Http(const Endpoints &ep, QObject *parent)
    : QObject(parent)
    , m_ep(ep)
    , m_nam(new QNetworkAccessManager(this))
    , m_launchId(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_heartbeatSessionId(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    m_nam->setAutoDeleteReplies(true);
    m_nam->setTransferTimeout(30000);
}

void Http::setBuildNumber(int n) {
    m_buildNumber = n;
}

QJsonObject Http::identifyProperties() const {
    QJsonObject p;
    p["os"] = "Windows";
    p["browser"] = "Chrome";
    p["device"] = "";
    p["system_locale"] = QLocale::system().name().replace('_', '-');
    p["has_client_mods"] = false;
    p["browser_user_agent"] = userAgent();
    p["browser_version"] = browserVersion();
    p["os_version"] = "10";
    p["referrer"] = "";
    p["referring_domain"] = "";
    p["referrer_current"] = "";
    p["referring_domain_current"] = "";
    p["release_channel"] = "stable";
    p["client_build_number"] = m_buildNumber;
    p["client_event_source"] = QJsonValue::Null;
    p["client_launch_id"] = m_launchId;
    p["client_heartbeat_session_id"] = m_heartbeatSessionId;
    p["client_app_state"] = "focused";
    return p;
}

QByteArray Http::superPropertiesJson() const {
    return QJsonDocument(identifyProperties()).toJson(QJsonDocument::Compact);
}

QNetworkRequest Http::makeRequest(const QString &path) const {
    QNetworkRequest req(QUrl(path.startsWith("http") ? path : m_ep.api + path));
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setRawHeader("Accept", "*/*");
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    if (!m_token.isEmpty()) req.setRawHeader("Authorization", m_token.toUtf8());
    req.setRawHeader("X-Super-Properties", superPropertiesJson().toBase64());
    req.setRawHeader("X-Discord-Locale", QLocale::system().name().replace('_', '-').toUtf8());
    req.setRawHeader("X-Discord-Timezone", QTimeZone::systemTimeZoneId());
    req.setRawHeader("X-Debug-Options", "bugReporterEnabled");
    req.setRawHeader("Origin", m_ep.web.toUtf8());
    req.setRawHeader("Referer", (m_ep.web + "/channels/@me").toUtf8());
    req.setRawHeader("Sec-Fetch-Dest", "empty");
    req.setRawHeader("Sec-Fetch-Mode", "cors");
    req.setRawHeader("Sec-Fetch-Site", "same-origin");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

void Http::send(const QByteArray &verb, const QString &path, const QByteArray &body, const QByteArray &contentType,
                HttpCallback cb, int attempt) {
    auto req = makeRequest(path);
    if (!contentType.isEmpty()) req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    QNetworkReply *reply = m_nam->sendCustomRequest(req, verb, body);
    connect(reply, &QNetworkReply::finished, this, [=, this]() {
        HttpResponse r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.body = reply->readAll();
        r.json = QJsonDocument::fromJson(r.body);
        if (reply->error() != QNetworkReply::NoError && r.status == 0) r.error = reply->errorString();

        // Rate limited: wait the time Discord asks for and retry (a few times at most).
        if (r.status == 429 && attempt < 3) {
            double wait = r.json.object().value("retry_after").toDouble(1.0);
            QTimer::singleShot(static_cast<int>(qBound(0.1, wait, 30.0) * 1000), this, [=, this]() {
                send(verb, path, body, contentType, cb, attempt + 1);
            });
            return;
        }
        if (!r.ok()) {
            qWarning().noquote() << "HTTP" << verb << path << "->" << r.status << r.error << r.body.left(300);
        }
        if (cb) cb(r);
    });
}

void Http::get(const QString &path, HttpCallback cb) {
    send("GET", path, {}, {}, std::move(cb));
}

void Http::post(const QString &path, const QJsonObject &body, HttpCallback cb) {
    send("POST", path, QJsonDocument(body).toJson(QJsonDocument::Compact), "application/json", std::move(cb));
}

void Http::postEmpty(const QString &path, HttpCallback cb) {
    send("POST", path, {}, {}, std::move(cb));
}

void Http::patch(const QString &path, const QJsonObject &body, HttpCallback cb) {
    send("PATCH", path, QJsonDocument(body).toJson(QJsonDocument::Compact), "application/json", std::move(cb));
}

void Http::put(const QString &path, HttpCallback cb) {
    send("PUT", path, {}, {}, std::move(cb));
}

void Http::del(const QString &path, HttpCallback cb) {
    send("DELETE", path, {}, {}, std::move(cb));
}

void Http::postMultipart(const QString &path, const QJsonObject &payload, const QStringList &files, HttpCallback cb) {
    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QJsonObject body = payload;
    QJsonArray attachments;
    for (int i = 0; i < files.size(); i++) {
        QJsonObject a;
        a["id"] = i;
        a["filename"] = QFileInfo(files[i]).fileName();
        attachments.append(a);
    }
    body["attachments"] = attachments;

    QHttpPart json;
    json.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"payload_json\"");
    json.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    json.setBody(QJsonDocument(body).toJson(QJsonDocument::Compact));
    multi->append(json);

    QMimeDatabase mimes;
    for (int i = 0; i < files.size(); i++) {
        auto *file = new QFile(files[i], multi);
        if (!file->open(QIODevice::ReadOnly)) continue;
        QHttpPart part;
        QString name = QFileInfo(files[i]).fileName();
        name.replace('"', '_');
        part.setRawHeader("Content-Disposition", QString("form-data; name=\"files[%1]\"; filename=\"%2\"").arg(i).arg(name).toUtf8());
        part.setHeader(QNetworkRequest::ContentTypeHeader, mimes.mimeTypeForFile(files[i]).name());
        part.setBodyDevice(file);
        multi->append(part);
    }

    auto req = makeRequest(path);
    QNetworkReply *reply = m_nam->post(req, multi);
    multi->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        HttpResponse r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.body = reply->readAll();
        r.json = QJsonDocument::fromJson(r.body);
        if (cb) cb(r);
    });
}

void Http::bootstrap(std::function<void()> done) {
    QNetworkRequest req(QUrl(m_ep.web + "/app"));
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    req.setRawHeader("Sec-Fetch-Dest", "document");
    req.setRawHeader("Sec-Fetch-Mode", "navigate");
    req.setRawHeader("Sec-Fetch-Site", "none");
    req.setRawHeader("Sec-Fetch-User", "?1");
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done]() {
        // The cookie jar has now stored __dcfduid / __sdcfduid / __cfruid.
        fetchBuildNumber(reply->readAll(), done);
    });
}

void Http::fetchBuildNumber(const QByteArray &appPage, std::function<void()> done) {
    // The web client's build number lives in one of the JS bundles referenced
    // by the app page. Try the sentry bundle first (it's small), then the rest.
    QStringList scripts;
    static const QRegularExpression scriptRe(R"re(/assets/[A-Za-z0-9._-]+\.js)re");
    auto it = scriptRe.globalMatch(QString::fromUtf8(appPage));
    while (it.hasNext()) {
        const auto s = it.next().captured(0);
        if (scripts.contains(s)) continue;
        if (s.contains("sentry")) scripts.prepend(s);
        else scripts.append(s);
    }
    while (scripts.size() > 6) scripts.removeLast();

    auto tryNext = std::make_shared<std::function<void()>>();
    *tryNext = [this, scripts, done, tryNext, i = 0]() mutable {
        if (i >= scripts.size()) {
            qWarning() << "Could not determine client build number; using" << m_buildNumber;
            done();
            return;
        }
        QNetworkRequest req(QUrl(m_ep.web + scripts[i++]));
        req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, done, tryNext]() {
            static const QRegularExpression re(R"re((?:buildNumber["']?\s*[,:]\s*["']?|build_number["']?\s*[,:]\s*["']?)(\d{5,7}))re");
            const auto m = re.match(QString::fromUtf8(reply->readAll()));
            if (m.hasMatch()) {
                m_buildNumber = m.captured(1).toInt();
                qInfo() << "Client build number:" << m_buildNumber;
                done();
            } else {
                (*tryNext)();
            }
        });
    };
    (*tryNext)();
}

} // namespace kestrel
