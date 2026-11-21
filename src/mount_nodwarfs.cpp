// Used when RWZFS_WITH_DWARFS=OFF: the library builds without archive reading.
// A missing archive is ArchiveNotFound, as in the dwarfs build; any other
// archive fails with OpenFailed. Everything else (paths, host IO, overlay,
// creation, revision) still works.
#include <filesystem>
#include <string>

#include "rwzfs/fs/mount_table.h"
#include "rwzfs/mount.h"

namespace rwz {

namespace stdfs = std::filesystem;

namespace {

MountError no_archive_support(const stdfs::path& archive)
{
	std::error_code ec = {};
	return stdfs::exists(archive, ec) ? MountError::OpenFailed
	                                  : MountError::ArchiveNotFound;
}

} // namespace

MountError mount_archive(const stdfs::path& archive)
{
	return no_archive_support(archive);
}
MountError mount_archive(const stdfs::path& archive, const stdfs::path&)
{
	return no_archive_support(archive);
}
MountError mount_archive(const stdfs::path& archive, const stdfs::path&,
                         const mount_options&)
{
	return no_archive_support(archive);
}

bool unmount_archive(const stdfs::path& mountpoint)
{
	return rwz::fs::mounts::remove(mountpoint).has_value();
}

std::string to_string(MountError error)
{
	switch (error) {
	case MountError::None: return "none";
	case MountError::ArchiveNotFound: return "archive not found";
	case MountError::MountPointInvalid:
		return "mountpoint must be at or under archive root";
	case MountError::OpenFailed:
		return "failed to open archive (built without dwarfs)";
	case MountError::WriteRootInvalid:
		return "write root cannot be resolved";
	case MountError::WriteRootUnavailable:
		return "overlay directory could not be created";
	case MountError::ArchiveInUse:
		return "archive is in use: only one instance is allowed. If no other instance is running, delete the lock file .rwz.lock in the overlay directory and try again";
	}
	return "unknown";
}

} // namespace rwz
