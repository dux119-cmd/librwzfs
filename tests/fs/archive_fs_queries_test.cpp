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
    ASSERT_TRUE(mounts::add("/rwz", backend));
  }
  void TearDown() override {
    mounts::clear();
    rfs::cwd::clear();
  }
};

TEST_F(ArchiveFsQueries, CanonicalIsAbsoluteArchivePath) {
  std::error_code ec;
  EXPECT_EQ(rfs::canonical("DIR/../DIR/FILE.DAT", ec), fs::path("/rwz/DIR/FILE.DAT"));
  EXPECT_FALSE(ec);
}

TEST_F(ArchiveFsQueries, CanonicalMissingReportsEnoent) {
  std::error_code ec;
  EXPECT_EQ(rfs::canonical("/rwz/nope", ec), fs::path{});
  EXPECT_EQ(ec, std::errc::no_such_file_or_directory);
}

TEST_F(ArchiveFsQueries, EquivalentSameEntry) {
  std::error_code ec;
  EXPECT_TRUE(rfs::equivalent("/rwz/DIR/FILE.DAT", "/rwz/DIR/../DIR/FILE.DAT", ec));
  EXPECT_FALSE(ec);
  EXPECT_FALSE(rfs::equivalent("/rwz/DIR/FILE.DAT", "/rwz/DIR/OTHER.DAT", ec));
  EXPECT_FALSE(ec);
}

TEST_F(ArchiveFsQueries, EquivalentMissingReportsEnoent) {
  std::error_code ec;
  EXPECT_FALSE(rfs::equivalent("/rwz/nope", "/rwz/DIR/FILE.DAT", ec));
  EXPECT_EQ(ec, std::errc::no_such_file_or_directory);
}

TEST_F(ArchiveFsQueries, IsSymlinkDetectsLinks) {
  std::error_code ec;
  EXPECT_TRUE(rfs::is_symlink("/rwz/LINK", ec));
  EXPECT_FALSE(rfs::is_symlink("/rwz/DIR/FILE.DAT", ec));
  EXPECT_FALSE(ec);
  EXPECT_FALSE(rfs::is_symlink("/rwz/nope", ec));
  EXPECT_EQ(ec, std::errc::no_such_file_or_directory);
}

TEST_F(ArchiveFsQueries, IsEmptyForFilesAndDirectories) {
  std::error_code ec;
  EXPECT_TRUE(rfs::is_empty("/rwz/EMPTY.TXT", ec));
  EXPECT_FALSE(rfs::is_empty("/rwz/DIR/FILE.DAT", ec));
  EXPECT_TRUE(rfs::is_empty("/rwz/EMPTY_DIR", ec));
  EXPECT_FALSE(rfs::is_empty("/rwz/DIR", ec));
  EXPECT_FALSE(ec);
}

TEST_F(ArchiveFsQueries, ReadSymlinkReturnsTarget) {
  std::error_code ec;
  EXPECT_EQ(rfs::read_symlink("/rwz/LINK", ec), fs::path("DIR/FILE.DAT"));
  EXPECT_FALSE(ec);
  ec.clear();
  EXPECT_EQ(rfs::read_symlink("/rwz/DIR/FILE.DAT", ec), fs::path{});
  EXPECT_EQ(ec, std::errc::invalid_argument);
}

TEST_F(ArchiveFsQueries, MutationsReturnErofs) {
  std::error_code ec;
  (void)rfs::create_directory("/rwz/new", ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
  ec.clear();
  (void)rfs::create_directories("/rwz/a/b", ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
  ec.clear();
  (void)rfs::remove("/rwz/DIR/FILE.DAT", ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
  ec.clear();
  (void)rfs::remove_all("/rwz/DIR", ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
  ec.clear();
  rfs::rename("/rwz/DIR/FILE.DAT", "/rwz/DIR/MOVED.DAT", ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
  ec.clear();
  rfs::resize_file("/rwz/DIR/FILE.DAT", 0, ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
  ec.clear();
  rfs::permissions("/rwz/DIR/FILE.DAT", fs::perms::owner_write, ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
}

TEST_F(ArchiveFsQueries, CopyIntoArchiveIsReadOnly) {
  std::error_code ec;
  rfs::copy("/rwz/DIR/FILE.DAT", "/rwz/DIR/COPY.DAT", fs::copy_options::none, ec);
  EXPECT_EQ(ec, std::errc::read_only_file_system);
}

TEST_F(ArchiveFsQueries, LastWriteTimeReadsArchiveTimestamp) {
  std::error_code ec;
  (void)rfs::last_write_time("/rwz/DIR/FILE.DAT", ec);
  EXPECT_FALSE(ec);
}
