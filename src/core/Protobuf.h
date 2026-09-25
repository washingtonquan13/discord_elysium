#pragma once

#include <QString>
#include <QVector>
#include <cstdint>

namespace kestrel {

struct GuildFolder {
    qint64 id = 0; // 0 when the "folder" is a single ungrouped server
    QString name;
    qint64 color = -1;
    QVector<quint64> guildIds;
};

// Decodes just the guild folder layout out of the base64 "user_settings_proto"
// (PreloadedUserSettings) blob. Returns an empty list if anything looks off.
QVector<GuildFolder> decodeGuildFolders(const QByteArray &base64);

} // namespace kestrel
