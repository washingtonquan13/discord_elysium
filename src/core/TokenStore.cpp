#include "TokenStore.h"

#include <QFile>

#ifdef _WIN32
    #include <windows.h>
    #include <dpapi.h>
#endif

namespace kestrel {

TokenStore::TokenStore(const QString &dir)
    : m_path(dir + "/token.bin") {}

#ifdef _WIN32
static QByteArray protect(const QByteArray &in, bool encrypt) {
    DATA_BLOB input { static_cast<DWORD>(in.size()), reinterpret_cast<BYTE *>(const_cast<char *>(in.data())) };
    DATA_BLOB output {};
    BOOL ok = encrypt
        ? CryptProtectData(&input, L"Kestrel token", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)
        : CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    if (!ok) return {};
    QByteArray result(reinterpret_cast<const char *>(output.pbData), static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return result;
}
#endif

QString TokenStore::load() const {
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray data = f.readAll();
#ifdef _WIN32
    data = protect(data, false);
#endif
    return QString::fromUtf8(data).trimmed();
}

void TokenStore::save(const QString &token) const {
    QByteArray data = token.toUtf8();
#ifdef _WIN32
    data = protect(data, true);
    if (data.isEmpty()) return;
#endif
    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    f.write(data);
}

void TokenStore::clear() const {
    QFile::remove(m_path);
}

} // namespace kestrel
