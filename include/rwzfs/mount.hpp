#pragma once

#include <filesystem>
#include <string>

namespace rwzfs {

enum class MountError {
  None,
  ArchiveNotFound,
  MountPointNotFound,
  OpenFailed,
};

[[nodiscard]] MountError mount_archive(const std::filesystem::path& archive,
                                       const std::filesystem::path& mountpoint);

[[nodiscard]] std::string to_string(MountError error);

}  // namespace rwzfs
