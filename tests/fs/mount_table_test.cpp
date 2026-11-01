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
  EXPECT_FALSE(mounts::add("/mnt/arch"));
  EXPECT_FALSE(mounts::is_archive_path("/mnt/arch/x"));
}

TEST_F(MountTable, AcceptsArchiveRootItself) {
  EXPECT_TRUE(mounts::add("/rwz"));
  EXPECT_TRUE(mounts::is_archive_path("/rwz"));
}

TEST_F(MountTable, AcceptsMountUnderArchiveRoot) {
  EXPECT_TRUE(mounts::add("/rwz/data"));
  EXPECT_TRUE(mounts::is_archive_path("/rwz/data/a/b.txt"));
  EXPECT_FALSE(mounts::is_archive_path("/rwz/other"));
}

TEST_F(MountTable, IsValidMountpointIsPure) {
  EXPECT_TRUE(mounts::is_valid_mountpoint("/rwz/x"));
  EXPECT_FALSE(mounts::is_valid_mountpoint("/rwzx"));
  EXPECT_FALSE(mounts::is_valid_mountpoint("rwz"));
  EXPECT_FALSE(mounts::is_archive_path("/rwz/x"));  // validation does not register
}

TEST_F(MountTable, EmptyTableRoutesNothing) {
  EXPECT_FALSE(mounts::is_archive_path("/rwz/anything"));
}

TEST_F(MountTable, SiblingWithSharedPrefixIsNotArchivePath) {
  ASSERT_TRUE(mounts::add("/rwz"));
  EXPECT_FALSE(mounts::is_archive_path("/rwzx/y"));
}

TEST_F(MountTable, DotDotThatLeavesMountIsNotArchivePath) {
  ASSERT_TRUE(mounts::add("/rwz/data"));
  EXPECT_FALSE(mounts::is_archive_path("/rwz/data/../other"));
}

TEST_F(MountTable, DotDotThatStaysInsideIsArchivePath) {
  ASSERT_TRUE(mounts::add("/rwz/data"));
  EXPECT_TRUE(mounts::is_archive_path("/rwz/data/a/../b"));
}

TEST_F(MountTable, RelativePathIsNeverArchivePath) {
  ASSERT_TRUE(mounts::add("/rwz"));
  EXPECT_FALSE(mounts::is_archive_path("rwz/x"));
}

TEST_F(MountTable, ClearRemovesAllMounts) {
  ASSERT_TRUE(mounts::add("/rwz"));
  mounts::clear();
  EXPECT_FALSE(mounts::is_archive_path("/rwz/x"));
}

TEST_F(MountTable, FirstMountBecomesArchiveCwd) {
  ASSERT_TRUE(mounts::add("/rwz"));
  EXPECT_EQ(rwzfs::fs::cwd::archive_cwd(), fs::path("/rwz"));
  EXPECT_EQ(rwzfs::fs::resolve_lexical("DIR/FILE.DAT"), fs::path("/rwz/DIR/FILE.DAT"));
}

TEST_F(MountTable, LaterMountKeepsExistingArchiveCwd) {
  ASSERT_TRUE(mounts::add("/rwz"));
  ASSERT_TRUE(mounts::add("/rwz/data"));
  EXPECT_EQ(rwzfs::fs::cwd::archive_cwd(), fs::path("/rwz"));
}

TEST_F(MountTable, ClearRestoresHostRelativePaths) {
  ASSERT_TRUE(mounts::add("/rwz"));
  mounts::clear();
  EXPECT_FALSE(rwzfs::fs::cwd::archive_cwd().has_value());
  EXPECT_EQ(rwzfs::fs::resolve_lexical("x"), fs::path("x"));
}
