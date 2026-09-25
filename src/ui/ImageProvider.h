#pragma once

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQuickAsyncImageProvider>

namespace kestrel {

class RemoteImageResponse;

// Fetches remote images through a disk cache and decodes them directly at
// the size they're displayed at (never the full-resolution original), off the
// UI thread. A small in-memory LRU keeps recently shown avatars/icons hot.
class ImageLoader : public QObject {
    Q_OBJECT

public:
    explicit ImageLoader(const QString &cacheDir, QObject *parent = nullptr);

    Q_INVOKABLE void load(const QString &url, int width, int height, bool crop, QPointer<kestrel::RemoteImageResponse> response);
    QImage cached(const QString &key);
    void clearMemory();

private:
    QNetworkAccessManager *m_nam;
    QMutex m_mutex;
    QCache<QString, QImage> m_memory; // cost in KiB
};

class RemoteImageResponse : public QQuickImageResponse {
    Q_OBJECT

public:
    QQuickTextureFactory *textureFactory() const override;
    QString errorString() const override { return m_error; }
    void finish(const QImage &img, const QString &error = {});

private:
    QImage m_image;
    QString m_error;
};

class RemoteImageProvider : public QQuickAsyncImageProvider {
public:
    explicit RemoteImageProvider(ImageLoader *loader)
        : m_loader(loader) {}

    // id: "<w>x<h>/<mode>/<percent-encoded url>", mode 'c' = crop to fill, 'n' = fit
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    ImageLoader *m_loader;
};

} // namespace kestrel
