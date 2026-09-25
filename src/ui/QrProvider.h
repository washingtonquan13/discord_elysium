#pragma once

#include <QQuickImageProvider>

namespace kestrel {

// image://qr/<percent-encoded text> -> a crisp black-on-white QR code
class QrProvider : public QQuickImageProvider {
public:
    QrProvider()
        : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

} // namespace kestrel
