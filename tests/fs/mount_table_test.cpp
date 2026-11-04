#include "../support/platform_paths.hpp"
#include <gtest/gtest.h>

#include <filesystem>

#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/resolve.hpp"
#include "rwzfs/fs/mount_table.hpp"

namespace fs = std::filesystem;
namespace mounts = rwzfs::fs::mounts;

class MountTable : public ::testing::Test {
 protected:
  void SetUp() override { mounts::clear(); }
  void TearDown() override { mounts::clear(); }
};

TEST_F(MountTable, RejectsRelativeMountpoint) {
  EXPECT_FALSE(mounts::add("rwz/mount"));
}

TEST_F(MountTable, RejectsHostMountpointOutsideArchiveRoot) {
  EXPECT_FALSE(mounts::add(HOST_ROOT "mnt/arch"));
  EXPECT_FALSE(mounts::is_archive_path(HOST_ROOT "mnt/arch/x"));
}

TEST_F(MountTable, AcceptsArchiveRootItself) {
  EXPECT_TRUE(mounts::add(RWZ_ROOT ""));
  EXPECT_TRUE(mounts::is_archive_path(RWZ_ROOT ""));
}

TEST_F(MountTable, AcceptsMountUnderArchiveRoot) {
  EXPECT_TRUE(mounts::add(RWZ_ROOT "/data"));
  EXPECT_TRUE(mounts::is_archive_path(RWZ_ROOT "/data/a/b.txt"));
  EXPECT_FALSE(mounts::is_archive_path(RWZ_ROOT "/other"));
}

TEST_F(MountTable, IsValidMountpointIsPure) {
  EXPECT_TRUE(mounts::is_valid_mountpoint(RWZ_ROOT "/x"));
  EXPECT_FALSE(mounts::is_valid_mountpoint(HOST_ROOT "rwzx"));
  EXPECT_FALSE(mounts::is_valid_mountpoint("rwz"));
  EXPECT_FALSE(mounts::is_archive_path(RWZ_ROOT "/x"));  // validation does not register
}

TEST_F(MountTable, EmptyTableRoutesNothing) {
  EXPECT_FALSE(mounts::is_archive_path(RWZ_ROOT "/anything"));
}

TEST_F(MountTable, SiblingWithSharedPrefixIsNotArchivePath) {
  ASSERT_TRUE(mounts::add(RWZ_ROOT ""));
  EXPECT_FALSE(mounts::is_archive_path(HOST_ROOT "rwzx/y"));
}

TEST_F(MountTable, DotDotThatLeavesMountIsNotArchivePath) {
  ASSERT_TRUE(mounts::add(RWZ_ROOT "/data"));
  EXPECT_FALSE(mounts::is_archive_path(RWZ_ROOT "/data/../other"));
}

TEST_F(MountTable, DotDotThatStaysInsideIsArchivePath) {
  ASSERT_TRUE(mounts::add(RWZ_ROOT "/data"));
  EXPECT_TRUE(mounts::is_archive_path(RWZ_ROOT "/data/a/../b"));
}

TEST_F(MountTable, RelativePathIsNeverArchivePath) {
  ASSERT_TRUE(mounts::add(RWZ_ROOT ""));
  EXPECT_FALSE(mounts::is_archive_path("rwz/x"));
}

TEST_F(MountTable, ClearRemovesAllMounts) {
  ASSERT_TRUE(mounts::add(RWZ_ROOT ""));
  mounts::clear();
  EXPECT_FALSE(mounts::is_archive_path(RWZ_ROOT "/x"));
}

TEST_F(MountTable, FirstMountBecomesArchiveCwd) {
  ASSERT_TRUE(mounts::add(RWZ_ROOT ""));
  EXPECT_EQ(rwzfs::fs::cwd::archive_cwd(), fs::path(RWZ_ROOT ""));
  EXPECT_EQ(rwzfs::fs::resolve_lexical("DIR/FILE.DAT"), fs::path(RWZ_ROOT "/DIR/FILE.DAT"));
}

TEST_F(MountTable, LaterMountKeepsExistingArchiveCwd) {
  ASSERT_TRUE(mounts::add(RWZ_ROOT ""));
  ASSERT_TRUE(mounts::add(RWZ_ROOT "/data"));
  EXPECT_EQ(rwzfs::fs::cwd::archive_cwd(), fs::path(RWZ_ROOT ""));
}

TEST_F(MountTable, ClearRestoresHostRelativePaths) {
  ASSERT_TRUE(mounts::add(RWZ_ROOT ""));
  mounts::clear();
  EXPECT_FALSE(rwzfs::fs::cwd::archive_cwd().has_value());
  EXPECT_EQ(rwzfs::fs::resolve_lexical("x"), fs::path("x"));
}
