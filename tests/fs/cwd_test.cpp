#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <system_error>

#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/filesystem.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "test_support.hpp"

namespace fs = std::filesystem;
namespace rfs = rwzfs::fs;
namespace mounts = rwzfs::fs::mounts;

class CwdTest : public ::testing::Test {
 protected:
  void SetUp() override {
    original_ = fs::current_path();
    mounts::clear();
    rfs::cwd::clear();
    ASSERT_TRUE(mounts::add("/rwzfs-test-mount"));
    ASSERT_TRUE(rfs::cwd::set_archive_cwd("/rwzfs-test-mount/sub"));
  }
  void TearDown() override {
    rfs::cwd::clear();
    mounts::clear();
    std::error_code ec;
    fs::current_path(original_, ec);
  }

  fs::path original_;
  TempDir dir_;
};

// CWD-05: resolve

TEST(CwdResolve, AbsoluteUnchangedWithoutArchiveCwd) {
  rfs::cwd::clear();
  EXPECT_EQ(rfs::resolve("/abs/x"), fs::path("/abs/x"));
}

TEST(CwdResolve, RelativeUnchangedWithoutArchiveCwd) {
  rfs::cwd::clear();
  EXPECT_EQ(rfs::resolve("x"), fs::path("x"));
}

TEST_F(CwdTest, RelativeResolvesAgainstArchiveCwd) {
  EXPECT_EQ(rfs::resolve("x"), fs::path("/rwzfs-test-mount/sub/x"));
}

TEST_F(CwdTest, DotAndDotDotNormalizeLexically) {
  EXPECT_EQ(rfs::resolve("./a/../b"), fs::path("/rwzfs-test-mount/sub/b"));
  EXPECT_EQ(rfs::resolve("."), fs::path("/rwzfs-test-mount/sub"));
}

TEST_F(CwdTest, DotDotCanLeaveArchiveToHostPath) {
  EXPECT_EQ(rfs::resolve("../../host/x"), fs::path("/host/x"));
}

TEST_F(CwdTest, AbsoluteIgnoresArchiveCwd) {
  EXPECT_EQ(rfs::resolve("/abs/x"), fs::path("/abs/x"));
}

// CWD-05 / CWD-01 / CWD-02: state

TEST(CwdState, SetRejectsPathOutsideMount) {
  mounts::clear();
  rfs::cwd::clear();
  ASSERT_TRUE(mounts::add("/rwzfs-test-mount"));
  EXPECT_FALSE(rfs::cwd::set_archive_cwd("/elsewhere"));
  EXPECT_FALSE(rfs::cwd::archive_cwd().has_value());
  mounts::clear();
}

TEST(CwdState, SetRejectsRelativePath) {
  mounts::clear();
  rfs::cwd::clear();
  ASSERT_TRUE(mounts::add("/rwzfs-test-mount"));
  EXPECT_FALSE(rfs::cwd::set_archive_cwd("sub"));
  EXPECT_FALSE(rfs::cwd::archive_cwd().has_value());
  mounts::clear();
}

TEST_F(CwdTest, CurrentPathReturnsArchiveCwd) {
  std::error_code ec;
  EXPECT_EQ(rfs::current_path(ec), fs::path("/rwzfs-test-mount/sub"));
  EXPECT_FALSE(ec);
}

TEST_F(CwdTest, ArchiveTargetIsRejectedAndCwdUnchanged) {
  std::error_code ec;
  rfs::current_path(fs::path("/rwzfs-test-mount/other"), ec);
  EXPECT_EQ(ec, std::errc::not_supported);
  EXPECT_EQ(rfs::cwd::archive_cwd(), fs::path("/rwzfs-test-mount/sub"));
}

TEST_F(CwdTest, HostTargetClearsArchiveCwd) {
  std::error_code ec;
  rfs::current_path(dir_.path(), ec);
  EXPECT_FALSE(ec);
  EXPECT_FALSE(rfs::cwd::archive_cwd().has_value());
  EXPECT_EQ(fs::canonical(fs::current_path()), fs::canonical(dir_.path()));
}

TEST_F(CwdTest, RelativeHostChdirWorksAfterClear) {
  fs::create_directories(dir_.path() / "inner");
  std::error_code ec;
  rfs::current_path(dir_.path(), ec);
  rfs::current_path(fs::path("inner"), ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(fs::canonical(fs::current_path()), fs::canonical(dir_.path() / "inner"));
}

TEST_F(CwdTest, AbsoluteIsLexicalUnderArchiveCwd) {
  std::error_code ec;
  EXPECT_EQ(rfs::absolute(fs::path("x/../y"), ec), fs::path("/rwzfs-test-mount/sub/y"));
  EXPECT_FALSE(ec);
}
