#pragma once

#include "Types.h"

namespace kestrel::perm {

constexpr quint64 CreateInvite = 1ULL << 0;
constexpr quint64 Administrator = 1ULL << 3;
constexpr quint64 ManageChannels = 1ULL << 4;
constexpr quint64 AddReactions = 1ULL << 6;
constexpr quint64 ViewChannel = 1ULL << 10;
constexpr quint64 SendMessages = 1ULL << 11;
constexpr quint64 ManageMessages = 1ULL << 13;
constexpr quint64 EmbedLinks = 1ULL << 14;
constexpr quint64 AttachFiles = 1ULL << 15;
constexpr quint64 ReadHistory = 1ULL << 16;
constexpr quint64 MentionEveryone = 1ULL << 17;
constexpr quint64 Connect = 1ULL << 20;
constexpr quint64 Speak = 1ULL << 21;
constexpr quint64 UseVAD = 1ULL << 25;
constexpr quint64 All = ~0ULL;

// Effective permissions of a member in a guild, and in a specific channel,
// following Discord's documented algorithm (base -> @everyone overwrite ->
// role overwrites -> member overwrite).
quint64 basePermissions(const Guild &g, Snowflake userId, const QVector<Snowflake> &roles);
quint64 channelPermissions(const Guild &g, const Channel &c, Snowflake userId, const QVector<Snowflake> &roles);

} // namespace kestrel::perm
