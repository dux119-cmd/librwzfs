#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <system_error>

#include "../support/fake_backend.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/filesystem.hpp"
#include "rwzfs/fs/mount_table.hpp"

namespace fs = std::filesystem;
namespace rfs = rwzfs::fs;
namespace mounts = rwzfs::fs::mounts;

class ArchiveFs : public ::testing::Test {
 protected:
  void SetUp() override {
    mounts::clear();
    rfs::cwd::clear();
    auto backend = std::make_shared<fake_backend>();
    backend->add_file("DIR/FILE.DAT", "payload");
    ASSERT_TRUE(mounts::add("/rwz", backend));
  }
  void TearDown() override {
    mounts::clear();
    rfs::cwd::clear();
  }
};

TEST_F(ArchiveFs, ExistsForFileAndMissing) {
  std::error_code ec;
  EXPECT_TRUE(rfs::exists("/rwz/DIR/FILE.DAT", ec));
  EXPECT_FALSE(ec);
  EXPECT_FALSE(rfs::exists("/rwz/nope", ec));
  EXPECT_FALSE(ec);
}

TEST_F(ArchiveFs, ExistsRelativeAfterMount) {
  std::error_code ec;
  EXPECT_TRUE(rfs::exists("DIR/FILE.DAT", ec));
  EXPECT_FALSE(ec);
}

TEST_F(ArchiveFs, StatusReportsTypeAndPermissions) {
  std::error_code ec;
  const auto file = rfs::status("/rwz/DIR/FILE.DAT", ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(file.type(), fs::file_type::regular);
  EXPECT_EQ(file.permissions(), fs::perms::owner_read | fs::perms::owner_write |
                                    fs::perms::group_read | fs::perms::others_read);
  const auto dir = rfs::status("/rwz/DIR", ec);
  EXPECT_EQ(dir.type(), fs::file_type::directory);
}

TEST_F(ArchiveFs, StatusMissingMatchesStdNotFound) {
  std::error_code ec;
  const auto st = rfs::status("/rwz/nope", ec);
  EXPECT_EQ(st.type(), fs::file_type::not_found);
  EXPECT_EQ(ec, std::errc::no_such_file_or_directory);
}

TEST_F(ArchiveFs, IsRegularFileAndIsDirectory) {
  std::error_code ec;
  EXPECT_TRUE(rfs::is_regular_file("/rwz/DIR/FILE.DAT", ec));
  EXPECT_FALSE(rfs::is_directory("/rwz/DIR/FILE.DAT", ec));
  EXPECT_TRUE(rfs::is_directory("/rwz/DIR", ec));
  EXPECT_FALSE(rfs::is_regular_file("/rwz/DIR", ec));
  EXPECT_FALSE(ec);
}

TEST_F(ArchiveFs, IsRegularFileMissingReportsEnoent) {
  std::error_code ec;
  EXPECT_FALSE(rfs::is_regular_file("/rwz/nope", ec));
  EXPECT_EQ(ec, std::errc::no_such_file_or_directory);
}

TEST_F(ArchiveFs, MutatingWrappersReportReadOnly) {
  std::error_code ec;
  EXPECT_EQ(rfs::remove_all("/rwz/DIR", ec), 0u);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
}

TEST_F(ArchiveFs, ChangeDirectoryVerifiesArchiveDirectory) {
  std::error_code ec;
  rfs::current_path(fs::path("/rwz/DIR"), ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(rfs::current_path(ec), fs::path("/rwz/DIR"));
  EXPECT_TRUE(rfs::exists("FILE.DAT", ec));
}

TEST_F(ArchiveFs, ChangeDirectoryRejectsFileAndMissing) {
  std::error_code ec;
  rfs::current_path(fs::path("/rwz/DIR/FILE.DAT"), ec);
  EXPECT_EQ(ec, std::errc::not_a_directory);
  EXPECT_EQ(rfs::cwd::archive_cwd(), fs::path("/rwz"));
  ec.clear();
  rfs::current_path(fs::path("/rwz/nope"), ec);
  EXPECT_EQ(ec, std::errc::no_such_file_or_directory);
}

TEST(MountRouting, LongestMountpointWins) {
  mounts::clear();
  auto outer = std::make_shared<fake_backend>();
  auto inner = std::make_shared<fake_backend>();
  inner->add_file("x", "inner");
  ASSERT_TRUE(mounts::add("/rwz", outer));
  ASSERT_TRUE(mounts::add("/rwz/data", inner));

  const auto r = mounts::find("/rwz/data/x");
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->backend, std::static_pointer_cast<const rwzfs::archive::backend>(inner));
  EXPECT_EQ(r->rel, "x");

  const auto root = mounts::find("/rwz/other");
  ASSERT_TRUE(root.has_value());
  EXPECT_EQ(root->rel, "other");
  EXPECT_FALSE(mounts::find("/host/x").has_value());
  mounts::clear();
}
