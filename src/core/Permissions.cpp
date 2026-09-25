#include "Permissions.h"

namespace kestrel::perm {

quint64 basePermissions(const Guild &g, Snowflake userId, const QVector<Snowflake> &roles) {
    if (g.ownerId == userId) return All;
    quint64 p = 0;
    if (auto it = g.roles.find(g.id); it != g.roles.end()) p = it->permissions; // @everyone
    for (auto r : roles) {
        if (auto it = g.roles.find(r); it != g.roles.end()) p |= it->permissions;
    }
    if (p & Administrator) return All;
    return p;
}

quint64 channelPermissions(const Guild &g, const Channel &c, Snowflake userId, const QVector<Snowflake> &roles) {
    quint64 p = basePermissions(g, userId, roles);
    if (p == All) return All;

    for (const auto &ow : c.overwrites) {
        if (ow.type == 0 && ow.id == g.id) {
            p &= ~ow.deny;
            p |= ow.allow;
            break;
        }
    }
    quint64 allow = 0, deny = 0;
    for (const auto &ow : c.overwrites) {
        if (ow.type == 0 && ow.id != g.id && roles.contains(ow.id)) {
            allow |= ow.allow;
            deny |= ow.deny;
        }
    }
    p &= ~deny;
    p |= allow;
    for (const auto &ow : c.overwrites) {
        if (ow.type == 1 && ow.id == userId) {
            p &= ~ow.deny;
            p |= ow.allow;
            break;
        }
    }
    return p;
}

} // namespace kestrel::perm
