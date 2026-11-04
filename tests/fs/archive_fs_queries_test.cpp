#include "../support/platform_paths.hpp"
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

class ArchiveFsQueries : public ::testing::Test {
 protected:
  void SetUp() override {
    mounts::clear();
    rfs::cwd::clear();
    auto backend = std::make_shared<fake_backend>();
    backend->add_file("DIR/FILE.DAT", "payload");
    backend->add_file("DIR/OTHER.DAT", "other");
    backend->add_file("EMPTY.TXT", "");
    backend->add_dir("EMPTY_DIR");
    backend->add_symlink("LINK", "DIR/FILE.DAT");
    ASSERT_TRUE(mounts::add(RWZ_ROOT "", backend));
  }
  void TearDown() override {
    mounts::clear();
    rfs::cwd::clear();
  }
};

TEST_F(ArchiveFsQueries, CanonicalIsAbsoluteArchivePath) {
  std::error_code ec;
  EXPECT_EQ(rfs::canonical("DIR/../DIR/FILE.DAT", ec), fs::path(RWZ_ROOT "/DIR/FILE.DAT"));
  EXPECT_FALSE(ec);
}

TEST_F(ArchiveFsQueries, CanonicalMissingReportsEnoent) {
  std::error_code ec;
  EXPECT_EQ(rfs::canonical(RWZ_ROOT "/nope", ec), fs::path{});
  EXPECT_EQ(ec, std::errc::no_such_file_or_directory);
}

TEST_F(ArchiveFsQueries, EquivalentSameEntry) {
  std::error_code ec;
  EXPECT_TRUE(rfs::equivalent(RWZ_ROOT "/DIR/FILE.DAT", RWZ_ROOT "/DIR/../DIR/FILE.DAT", ec));
  EXPECT_FALSE(ec);
  EXPECT_FALSE(rfs::equivalent(RWZ_ROOT "/DIR/FILE.DAT", RWZ_ROOT "/DIR/OTHER.DAT", ec));
  EXPECT_FALSE(ec);
}

TEST_F(ArchiveFsQueries, EquivalentMissingReportsEnoent) {
  std::error_code ec;
  EXPECT_FALSE(rfs::equivalent(RWZ_ROOT "/nope", RWZ_ROOT "/DIR/FILE.DAT", ec));
  EXPECT_EQ(ec, std::errc::no_such_file_or_directory);
}

TEST_F(ArchiveFsQueries, IsSymlinkDetectsLinks) {
  std::error_code ec;
  EXPECT_TRUE(rfs::is_symlink(RWZ_ROOT "/LINK", ec));
  EXPECT_FALSE(rfs::is_symlink(RWZ_ROOT "/DIR/FILE.DAT", ec));
  EXPECT_FALSE(ec);
  EXPECT_FALSE(rfs::is_symlink(RWZ_ROOT "/nope", ec));
  EXPECT_EQ(ec, std::errc::no_such_file_or_directory);
}

TEST_F(ArchiveFsQueries, IsEmptyForFilesAndDirectories) {
  std::error_code ec;
  EXPECT_TRUE(rfs::is_empty(RWZ_ROOT "/EMPTY.TXT", ec));
  EXPECT_FALSE(rfs::is_empty(RWZ_ROOT "/DIR/FILE.DAT", ec));
  EXPECT_TRUE(rfs::is_empty(RWZ_ROOT "/EMPTY_DIR", ec));
  EXPECT_FALSE(rfs::is_empty(RWZ_ROOT "/DIR", ec));
  EXPECT_FALSE(ec);
}

TEST_F(ArchiveFsQueries, ReadSymlinkReturnsTarget) {
  std::error_code ec;
  EXPECT_EQ(rfs::read_symlink(RWZ_ROOT "/LINK", ec), fs::path("DIR/FILE.DAT"));
  EXPECT_FALSE(ec);
  ec.clear();
  EXPECT_EQ(rfs::read_symlink(RWZ_ROOT "/DIR/FILE.DAT", ec), fs::path{});
  EXPECT_EQ(ec, std::errc::invalid_argument);
}

TEST_F(ArchiveFsQueries, MutationsReturnErofs) {
  std::error_code ec;
  (void)rfs::create_directory(RWZ_ROOT "/new", ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
  ec.clear();
  (void)rfs::create_directories(RWZ_ROOT "/a/b", ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
  ec.clear();
  (void)rfs::remove(RWZ_ROOT "/DIR/FILE.DAT", ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
  ec.clear();
  (void)rfs::remove_all(RWZ_ROOT "/DIR", ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
  ec.clear();
  rfs::rename(RWZ_ROOT "/DIR/FILE.DAT", RWZ_ROOT "/DIR/MOVED.DAT", ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
  ec.clear();
  rfs::resize_file(RWZ_ROOT "/DIR/FILE.DAT", 0, ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
  ec.clear();
  rfs::permissions(RWZ_ROOT "/DIR/FILE.DAT", fs::perms::owner_write, ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
}

TEST_F(ArchiveFsQueries, CopyIntoArchiveIsReadOnly) {
  std::error_code ec;
  rfs::copy(RWZ_ROOT "/DIR/FILE.DAT", RWZ_ROOT "/DIR/COPY.DAT", fs::copy_options::none, ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
}

TEST_F(ArchiveFsQueries, LastWriteTimeReadsArchiveTimestamp) {
  std::error_code ec;
  (void)rfs::last_write_time(RWZ_ROOT "/DIR/FILE.DAT", ec);
  EXPECT_FALSE(ec);
}
