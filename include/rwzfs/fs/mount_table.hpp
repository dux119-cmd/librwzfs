#pragma once

#include <filesystem>

namespace rwzfs::fs::mounts {

// Registers an absolute mountpoint. Returns false for a relative path.
[[nodiscard]] bool add(const std::filesystem::path& mountpoint);

// Removes all mountpoints.
void clear();

// True if the path, after lexical normalization, is at or under a mountpoint.
// Never touches storage. Relative paths are never archive paths.
[[nodiscard]] bool is_archive_path(const std::filesystem::path& p);

}  // namespace rwzfs::fs::mounts
