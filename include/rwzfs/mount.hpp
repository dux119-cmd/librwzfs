#pragma once

#include <filesystem>
#include <string>

namespace rwzfs {

enum class MountError {
  None,
  ArchiveNotFound,
  MountPointInvalid,
  OpenFailed,
};

// Mounts an archive at the archive root ("/rwz"), so relative paths resolve into it.
[[nodiscard]] MountError mount_archive(const std::filesystem::path& archive);

// Mounts an archive at a virtual mountpoint under archive_root() (e.g. "/rwz/data").
[[nodiscard]] MountError mount_archive(const std::filesystem::path& archive,
                                       const std::filesystem::path& mountpoint);

[[nodiscard]] std::string to_string(MountError error);

}  // namespace rwzfs
