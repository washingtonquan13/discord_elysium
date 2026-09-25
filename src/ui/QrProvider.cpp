#include "QrProvider.h"

#include <QPainter>
#include <QUrl>
#include <qrcodegen.hpp>

namespace kestrel {

QImage QrProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize) {
    const QString text = QUrl::fromPercentEncoding(id.toLatin1());
    const auto qr = qrcodegen::QrCode::encodeText(text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int border = 2;
    const int modules = qr.getSize() + border * 2;
    const int side = requestedSize.width() > 0 ? requestedSize.width() : 256;
    const int scale = qMax(1, side / modules);

    QImage img(modules * scale, modules * scale, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    for (int y = 0; y < qr.getSize(); y++)
        for (int x = 0; x < qr.getSize(); x++)
            if (qr.getModule(x, y)) p.fillRect((x + border) * scale, (y + border) * scale, scale, scale, Qt::black);
    p.end();
    if (size) *size = img.size();
    return img;
}

} // namespace kestrel
