#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "rwzfs/mount.hpp"

namespace fs = std::filesystem;

TEST(MountArchive, MissingArchiveIsReported) {
  const auto dir = fs::temp_directory_path();
  EXPECT_EQ(rwzfs::mount_archive(dir / "does_not_exist.dwarfs", dir),
            rwzfs::MountError::ArchiveNotFound);
}

TEST(MountArchive, MissingMountPointIsReported) {
  const auto archive = fs::temp_directory_path() / "rwzfs_probe.dwarfs";
  {
    std::ofstream(archive).put('\0');
  }
  EXPECT_EQ(rwzfs::mount_archive(archive, "/does/not/exist"),
            rwzfs::MountError::MountPointNotFound);
  fs::remove(archive);
}

TEST(MountArchive, InvalidImageFailsToOpen) {
  const auto archive = fs::temp_directory_path() / "rwzfs_garbage.dwarfs";
  {
    std::ofstream(archive) << "not a dwarfs image";
  }
  EXPECT_EQ(rwzfs::mount_archive(archive, fs::temp_directory_path()),
            rwzfs::MountError::OpenFailed);
  fs::remove(archive);
}

// Requires RWZFS_TEST_ARCHIVE=/path/to/image.dwarfs. Skipped otherwise.
TEST(MountArchive, ValidArchiveOpens) {
  const char* archive = std::getenv("RWZFS_TEST_ARCHIVE");
  if (archive == nullptr) {
    GTEST_SKIP() << "RWZFS_TEST_ARCHIVE not set";
  }
  EXPECT_EQ(rwzfs::mount_archive(archive, fs::temp_directory_path()),
            rwzfs::MountError::None);
}
