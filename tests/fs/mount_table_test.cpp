#include <gtest/gtest.h>

#include <filesystem>

#include "rwzfs/fs/mount_table.hpp"

namespace fs = std::filesystem;
namespace mounts = rwzfs::fs::mounts;

class MountTable : public ::testing::Test {
 protected:
  void SetUp() override { mounts::clear(); }
  void TearDown() override { mounts::clear(); }
};

TEST_F(MountTable, RejectsRelativeMountpoint) {
  EXPECT_FALSE(mounts::add("relative/mount"));
}

TEST_F(MountTable, EmptyTableRoutesNothing) {
  EXPECT_FALSE(mounts::is_archive_path("/anything"));
}

TEST_F(MountTable, MountpointItselfIsArchivePath) {
  ASSERT_TRUE(mounts::add("/mnt/arch"));
  EXPECT_TRUE(mounts::is_archive_path("/mnt/arch"));
}

TEST_F(MountTable, DescendantsAreArchivePaths) {
  ASSERT_TRUE(mounts::add("/mnt/arch"));
  EXPECT_TRUE(mounts::is_archive_path("/mnt/arch/a/b.txt"));
}

TEST_F(MountTable, SiblingWithSharedPrefixIsNotArchivePath) {
  ASSERT_TRUE(mounts::add("/mnt/arch"));
  EXPECT_FALSE(mounts::is_archive_path("/mnt/archive2/x"));
}

TEST_F(MountTable, DotDotThatLeavesMountIsNotArchivePath) {
  ASSERT_TRUE(mounts::add("/mnt/arch"));
  EXPECT_FALSE(mounts::is_archive_path("/mnt/arch/../other"));
}

TEST_F(MountTable, DotDotThatStaysInsideIsArchivePath) {
  ASSERT_TRUE(mounts::add("/mnt/arch"));
  EXPECT_TRUE(mounts::is_archive_path("/mnt/arch/a/../b"));
}

TEST_F(MountTable, RelativePathIsNeverArchivePath) {
  ASSERT_TRUE(mounts::add("/mnt/arch"));
  EXPECT_FALSE(mounts::is_archive_path("mnt/arch/x"));
}

TEST_F(MountTable, ClearRemovesAllMounts) {
  ASSERT_TRUE(mounts::add("/mnt/arch"));
  mounts::clear();
  EXPECT_FALSE(mounts::is_archive_path("/mnt/arch/x"));
}
