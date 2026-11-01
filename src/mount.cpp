#include "rwzfs/mount.hpp"

#include <exception>
#include <filesystem>
#include <memory>

#include "rwzfs/archive/dwarfs_backend.hpp"
#include "rwzfs/fs/archive_path.hpp"
#include "rwzfs/fs/mount_table.hpp"

namespace rwzfs {

namespace stdfs = std::filesystem;

MountError mount_archive(const stdfs::path& archive) {
  return mount_archive(archive, rwzfs::fs::archive_root());
}

MountError mount_archive(const stdfs::path& archive, const stdfs::path& mountpoint) {
  if (!stdfs::is_regular_file(archive)) {
    return MountError::ArchiveNotFound;
  }
  if (!rwzfs::fs::mounts::is_valid_mountpoint(mountpoint)) {
    return MountError::MountPointInvalid;
  }
  try {
    auto backend = std::make_shared<archive::dwarfs_backend>(archive);
    if (!rwzfs::fs::mounts::add(mountpoint, std::move(backend))) {
      return MountError::MountPointInvalid;
    }
  } catch (const std::exception&) {
    return MountError::OpenFailed;
  }
  return MountError::None;
}

std::string to_string(MountError error) {
  switch (error) {
    case MountError::None: return "none";
    case MountError::ArchiveNotFound: return "archive not found";
    case MountError::MountPointInvalid: return "mountpoint must be at or under archive root";
    case MountError::OpenFailed: return "failed to open archive";
  }
  return "unknown";
}

}  // namespace rwzfs
