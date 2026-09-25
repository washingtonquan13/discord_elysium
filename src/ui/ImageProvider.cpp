#include "ImageProvider.h"

#include "core/Http.h"

#include <QBuffer>
#include <QImageReader>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>

namespace kestrel {

ImageLoader::ImageLoader(const QString &cacheDir, QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this)) {
    auto *cache = new QNetworkDiskCache(this);
    cache->setCacheDirectory(cacheDir);
    cache->setMaximumCacheSize(256LL * 1024 * 1024);
    m_nam->setCache(cache);
    m_nam->setAutoDeleteReplies(true);
    m_nam->setTransferTimeout(30000);
    m_memory.setMaxCost(24 * 1024); // 24 MiB of decoded pixels
    qRegisterMetaType<QPointer<kestrel::RemoteImageResponse>>();
}

static QString cacheKey(const QString &url, int w, int h, bool crop) {
    return QString("%1|%2|%3|%4").arg(w).arg(h).arg(crop).arg(url);
}

QImage ImageLoader::cached(const QString &key) {
    QMutexLocker lock(&m_mutex);
    if (auto *img = m_memory.object(key)) return *img;
    return {};
}

void ImageLoader::clearMemory() {
    QMutexLocker lock(&m_mutex);
    m_memory.clear();
}

namespace {

class DecodeTask : public QRunnable {
public:
    DecodeTask(QByteArray data, int w, int h, bool crop, std::function<void(QImage)> done)
        : m_data(std::move(data)), m_w(w), m_h(h), m_crop(crop), m_done(std::move(done)) {}

    void run() override {
        QBuffer buf(&m_data);
        buf.open(QIODevice::ReadOnly);
        QImageReader reader(&buf);
        reader.setAutoTransform(true);
        QSize src = reader.size();
        if (src.isValid() && m_w > 0 && m_h > 0) {
            // Decode straight to the displayed size; the full image is never held in memory.
            QSize target = src.scaled(m_w, m_h, m_crop ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
            if (target.width() < src.width() || target.height() < src.height()) reader.setScaledSize(target);
        }
        QImage img = reader.read();
        if (!img.isNull() && m_w > 0 && m_h > 0) {
            if (m_crop && (img.width() != m_w || img.height() != m_h)) {
                img = img.scaled(m_w, m_h, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                img = img.copy((img.width() - m_w) / 2, (img.height() - m_h) / 2, m_w, m_h);
            } else if (!m_crop && (img.width() > m_w || img.height() > m_h)) {
                img = img.scaled(m_w, m_h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
        }
        if (!img.isNull() && img.format() != QImage::Format_ARGB32_Premultiplied && img.hasAlphaChannel())
            img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        m_done(img);
    }

private:
    QByteArray m_data;
    int m_w, m_h;
    bool m_crop;
    std::function<void(QImage)> m_done;
};

} // namespace

void ImageLoader::load(const QString &url, int width, int height, bool crop, QPointer<RemoteImageResponse> response) {
    if (!response) return;
    const QString key = cacheKey(url, width, height, crop);

    QNetworkRequest req { QUrl(url) };
    req.setHeader(QNetworkRequest::UserAgentHeader, Http::userAgent());
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [=, this]() {
        if (!response) return;
        if (reply->error() != QNetworkReply::NoError) {
            response->finish({}, reply->errorString());
            return;
        }
        auto *task = new DecodeTask(reply->readAll(), width, height, crop, [=, this](QImage img) {
            if (!img.isNull()) {
                QMutexLocker lock(&m_mutex);
                m_memory.insert(key, new QImage(img), qMax<qsizetype>(1, img.sizeInBytes() / 1024));
            }
            QMetaObject::invokeMethod(this, [response, img]() {
                if (response) response->finish(img, img.isNull() ? QString("decode failed") : QString());
            }, Qt::QueuedConnection);
        });
        QThreadPool::globalInstance()->start(task);
    });
}

QQuickTextureFactory *RemoteImageResponse::textureFactory() const {
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

void RemoteImageResponse::finish(const QImage &img, const QString &error) {
    m_image = img;
    m_error = error;
    emit finished();
}

QQuickImageResponse *RemoteImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
    auto *response = new RemoteImageResponse;
    // parse "<w>x<h>/<mode>/<url>"
    const int s1 = id.indexOf('/');
    const int s2 = id.indexOf('/', s1 + 1);
    if (s1 < 0 || s2 < 0) {
        response->finish({}, "bad image id");
        return response;
    }
    const auto dims = id.left(s1).split('x');
    int w = dims.value(0).toInt(), h = dims.value(1).toInt();
    if (requestedSize.isValid() && requestedSize.width() > 0) {
        w = requestedSize.width();
        h = requestedSize.height() > 0 ? requestedSize.height() : h;
    }
    const bool crop = id.mid(s1 + 1, s2 - s1 - 1) == "c";
    const QString url = QUrl::fromPercentEncoding(id.mid(s2 + 1).toLatin1());

    response->moveToThread(m_loader->thread());
    const QImage hit = m_loader->cached(cacheKey(url, w, h, crop));
    if (!hit.isNull()) {
        // finish asynchronously so the engine has connected to finished()
        QPointer<RemoteImageResponse> p(response);
        QMetaObject::invokeMethod(m_loader, [p, hit]() { if (p) p->finish(hit); }, Qt::QueuedConnection);
        return response;
    }
    QMetaObject::invokeMethod(m_loader, "load", Qt::QueuedConnection, Q_ARG(QString, url), Q_ARG(int, w), Q_ARG(int, h),
                              Q_ARG(bool, crop), Q_ARG(QPointer<kestrel::RemoteImageResponse>, QPointer<RemoteImageResponse>(response)));
    return response;
}

} // namespace kestrel
