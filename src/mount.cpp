#include "rwzfs/mount.hpp"

#include <exception>
#include <filesystem>
#include <memory>

#include "rwzfs/archive/dwarfs_backend.hpp"
#include "rwzfs/fs/archive_path.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/overlay.hpp"
#include "rwzfs/overlay/lock.hpp"

namespace rwzfs {

namespace stdfs = std::filesystem;

MountError mount_archive(const stdfs::path& archive) {
  return mount_archive(archive, rwzfs::fs::archive_root(), mount_options{});
}

MountError mount_archive(const stdfs::path& archive, const stdfs::path& mountpoint) {
  return mount_archive(archive, mountpoint, mount_options{});
}

MountError mount_archive(const stdfs::path& archive, const stdfs::path& mountpoint,
                         const mount_options& options) {
  if (!stdfs::is_regular_file(archive)) {
    return MountError::ArchiveNotFound;
  }
  if (!rwzfs::fs::mounts::is_valid_mountpoint(mountpoint)) {
    return MountError::MountPointInvalid;
  }
  const auto overlay_dir = overlay::overlay_directory(archive, options.write_root,
                                                      overlay::process_environment());
  if (!overlay_dir) {
    return overlay_dir.error() == overlay::overlay_error::write_root_relative
               ? MountError::WriteRootInvalid
               : MountError::WriteRootUnavailable;
  }
  std::error_code ec;
  stdfs::create_directories(*overlay_dir, ec);
  if (ec) {
    return MountError::WriteRootUnavailable;
  }
  if (const auto held = overlay::lock::acquire(*overlay_dir); !held) {
    return held.error() == overlay::lock::lock_error::in_use ? MountError::ArchiveInUse
                                                              : MountError::WriteRootUnavailable;
  }
  try {
    auto backend = std::make_shared<archive::dwarfs_backend>(archive);
    if (!rwzfs::fs::mounts::add(mountpoint, std::move(backend), *overlay_dir, archive)) {
      overlay::lock::release(*overlay_dir);
      return MountError::MountPointInvalid;
    }
  } catch (const std::exception&) {
    overlay::lock::release(*overlay_dir);
    return MountError::OpenFailed;
  }
  return MountError::None;
}

bool unmount_archive(const stdfs::path& mountpoint) {
  return rwzfs::fs::mounts::remove(mountpoint).has_value();
}

std::string to_string(MountError error) {
  switch (error) {
    case MountError::None: return "none";
    case MountError::ArchiveNotFound: return "archive not found";
    case MountError::MountPointInvalid: return "mountpoint must be at or under archive root";
    case MountError::OpenFailed: return "failed to open archive";
    case MountError::WriteRootInvalid: return "write root must be an absolute path";
    case MountError::WriteRootUnavailable: return "overlay directory could not be created";
    case MountError::ArchiveInUse:
      return "archive is in use: only one instance is allowed. If no other instance is running, delete the lock file .rwz.lock in the overlay directory and try again";
  }
  return "unknown";
}

}  // namespace rwzfs
