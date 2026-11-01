#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "rwzfs/archive/backend.hpp"

namespace rwzfs::fs::mounts {

// Result of routing an absolute archive path to a mount.
struct route {
  std::shared_ptr<const archive::backend> backend;  // null when mounted without an archive
  std::string rel;                                   // archive-relative text, "" for the mount root
  std::filesystem::path absolute;                    // normalized absolute archive path
  std::filesystem::path overlay;                     // overlay directory for writes (may be empty)
};

// True if p is absolute and at or under archive_root(). Pure.
[[nodiscard]] bool is_valid_mountpoint(const std::filesystem::path& p);

// Registers a mountpoint. Returns false unless is_valid_mountpoint(p).
// If no archive working directory is set, it becomes the mountpoint.
[[nodiscard]] bool add(const std::filesystem::path& mountpoint,
                       std::shared_ptr<const archive::backend> backend = nullptr,
                       std::filesystem::path overlay = {});

// Removes all mountpoints and the archive working directory.
void clear();

// True if the path, after walk_lexically, is at or under a mountpoint. Never touches storage.
[[nodiscard]] bool is_archive_path(const std::filesystem::path& p);

// Routes an absolute path to the longest matching mountpoint, or nullopt for host paths.
[[nodiscard]] std::optional<route> find(const std::filesystem::path& p);

}  // namespace rwzfs::fs::mounts
