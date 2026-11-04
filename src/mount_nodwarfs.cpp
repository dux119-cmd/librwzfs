// Used when RWZFS_WITH_DWARFS=OFF: the library builds without archive reading. Mounting always
// fails with OpenFailed. Everything else (paths, host IO, overlay, creation, revision) still works.
#include <filesystem>
#include <string>

#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/mount.hpp"

namespace rwzfs {

namespace stdfs = std::filesystem;

MountError mount_archive(const stdfs::path&) { return MountError::OpenFailed; }
MountError mount_archive(const stdfs::path&, const stdfs::path&) { return MountError::OpenFailed; }
MountError mount_archive(const stdfs::path&, const stdfs::path&, const mount_options&) {
  return MountError::OpenFailed;
}

bool unmount_archive(const stdfs::path& mountpoint) {
  return rwzfs::fs::mounts::remove(mountpoint).has_value();
}

std::string to_string(MountError error) {
  switch (error) {
    case MountError::None: return "none";
    case MountError::ArchiveNotFound: return "archive not found";
    case MountError::MountPointInvalid: return "mountpoint must be at or under archive root";
    case MountError::OpenFailed: return "failed to open archive (built without dwarfs)";
    case MountError::WriteRootInvalid: return "write root must be an absolute path";
    case MountError::WriteRootUnavailable: return "overlay directory could not be created";
    case MountError::ArchiveInUse:
      return "archive is in use: only one instance is allowed. If no other instance is running, delete the lock file .rwz.lock in the overlay directory and try again";
  }
  return "unknown";
}

}  // namespace rwzfs
