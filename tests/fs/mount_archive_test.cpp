#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "../archive/fixture.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/mount.hpp"
#include "test_support.hpp"

namespace fs = std::filesystem;
namespace mounts = rwzfs::fs::mounts;

class MountArchive : public ::testing::Test {
 protected:
  void SetUp() override { mounts::clear(); }
  void TearDown() override { mounts::clear(); }
};

TEST_F(MountArchive, MissingArchiveIsReported) {
  EXPECT_EQ(rwzfs::mount_archive(fs::path("/does/not/exist.dwarfs"), "/rwz"),
            rwzfs::MountError::ArchiveNotFound);
  EXPECT_FALSE(mounts::is_archive_path("/rwz/x"));
}

TEST_F(MountArchive, InvalidMountpointIsReportedBeforeOpening) {
  TempDir dir;
  const auto archive = dir.path() / "garbage.dwarfs";
  std::ofstream(archive) << "not a dwarfs image";
  EXPECT_EQ(rwzfs::mount_archive(archive, "/mnt/outside"), rwzfs::MountError::MountPointInvalid);
}

TEST_F(MountArchive, GarbageImageFailsToOpen) {
  TempDir dir;
  const auto archive = dir.path() / "garbage.dwarfs";
  std::ofstream(archive) << "not a dwarfs image";
  EXPECT_EQ(rwzfs::mount_archive(archive, "/rwz"), rwzfs::MountError::OpenFailed);
  EXPECT_FALSE(mounts::is_archive_path("/rwz/x"));
}

TEST_F(MountArchive, FixtureArchiveMountsAtRoot) {
  EXPECT_EQ(rwzfs::mount_archive(kFixtureArchive, "/rwz"), rwzfs::MountError::None);
  EXPECT_TRUE(mounts::is_archive_path("/rwz/anything"));
}
