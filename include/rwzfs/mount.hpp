#pragma once

#include <filesystem>
#include <string>

#include "rwzfs/overlay.hpp"

namespace rwzfs {

enum class MountError {
  None,
  ArchiveNotFound,
  MountPointInvalid,
  OpenFailed,
  WriteRootInvalid,      // mount_options::write_root is not absolute
  WriteRootUnavailable,  // the overlay directory could not be created
};

// Mounts an archive at the archive root ("/rwz") with default options.
[[nodiscard]] MountError mount_archive(const std::filesystem::path& archive);

// Mounts an archive at a virtual mountpoint under archive_root() (e.g. "/rwz/data").
[[nodiscard]] MountError mount_archive(const std::filesystem::path& archive,
                                       const std::filesystem::path& mountpoint);

// Full form. Creates the overlay directory `<write root>/<stem>-writes` if needed.
[[nodiscard]] MountError mount_archive(const std::filesystem::path& archive,
                                       const std::filesystem::path& mountpoint,
                                       const mount_options& options);

[[nodiscard]] std::string to_string(MountError error);

}  // namespace rwzfs
