#pragma once

#include <QString>

namespace kestrel {

// Stores the account token encrypted with the Windows Data Protection API
// (only the current Windows user can decrypt it). On other platforms it
// falls back to a file readable only by the owner.
class TokenStore {
public:
    explicit TokenStore(const QString &dir);

    QString load() const;
    void save(const QString &token) const;
    void clear() const;

private:
    QString m_path;
};

} // namespace kestrel
