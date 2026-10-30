#include "rwzfs/mount.hpp"

#include <exception>

#include <dwarfs/logger.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/reader/filesystem_v2.h>

namespace rwzfs {

namespace fs = std::filesystem;

namespace {

MountError open_image(const fs::path& archive) {
  try {
    dwarfs::os_access_generic os;
    dwarfs::null_logger lgr;
    [[maybe_unused]] dwarfs::reader::filesystem_v2 image(lgr, os, archive);
    return MountError::None;
  } catch (const std::exception&) {
    return MountError::OpenFailed;
  }
}

}  // namespace

MountError mount_archive(const fs::path& archive, const fs::path& mountpoint) {
  if (!fs::is_regular_file(archive)) {
    return MountError::ArchiveNotFound;
  }
  if (!fs::is_directory(mountpoint)) {
    return MountError::MountPointNotFound;
  }
  return open_image(archive);
}

std::string to_string(MountError error) {
  switch (error) {
    case MountError::None: return "none";
    case MountError::ArchiveNotFound: return "archive not found";
    case MountError::MountPointNotFound: return "mount point not found";
    case MountError::OpenFailed: return "failed to open archive";
  }
  return "unknown";
}

}  // namespace rwzfs
