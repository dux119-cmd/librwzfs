#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/filesystem.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "test_support.hpp"

namespace fs = std::filesystem;
namespace rfs = rwzfs::fs;
namespace mounts = rwzfs::fs::mounts;

namespace {

void write_file(const fs::path& p, const std::string& content) {
  std::ofstream(p) << content;
}

}  // namespace

// Passthrough: with no mounts, every wrapper must match the standard call.

class FsPassthrough : public ::testing::Test {
 protected:
  void SetUp() override {
    mounts::clear();
    rfs::cwd::clear();
    file_ = dir_.path() / "file.txt";
    write_file(file_, "hello");
  }
  void TearDown() override { mounts::clear();
    rfs::cwd::clear(); }

  TempDir dir_;
  fs::path file_;
};

TEST_F(FsPassthrough, ExistsMatchesStd) {
  std::error_code ec;
  EXPECT_TRUE(rfs::exists(file_, ec));
  EXPECT_FALSE(ec);
  EXPECT_FALSE(rfs::exists(dir_.path() / "missing", ec));
  EXPECT_FALSE(ec);
}

TEST_F(FsPassthrough, StatusMatchesStd) {
  std::error_code ec;
  EXPECT_EQ(rfs::status(file_, ec).type(), fs::file_type::regular);
  EXPECT_FALSE(ec);
}

TEST_F(FsPassthrough, CopyMatchesStd) {
  std::error_code ec;
  const auto dst = dir_.path() / "copy.txt";
  rfs::copy(file_, dst, fs::copy_options::none, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(fs::file_size(dst), 5u);
}

TEST_F(FsPassthrough, CreateDirectoriesMatchesStd) {
  std::error_code ec;
  const auto nested = dir_.path() / "a" / "b";
  EXPECT_TRUE(rfs::create_directories(nested, ec));
  EXPECT_FALSE(rfs::create_directories(nested, ec));  // already exists
  EXPECT_FALSE(ec);
}

TEST_F(FsPassthrough, IsRegularFileAndIsDirectoryMatchStd) {
  std::error_code ec;
  EXPECT_TRUE(rfs::is_regular_file(file_, ec));
  EXPECT_FALSE(rfs::is_directory(file_, ec));
  EXPECT_TRUE(rfs::is_directory(dir_.path(), ec));
}

TEST_F(FsPassthrough, AbsoluteMatchesStd) {
  std::error_code ec;
  EXPECT_EQ(rfs::absolute(fs::path("rel/x"), ec), fs::absolute("rel/x"));
}

TEST_F(FsPassthrough, CanonicalMatchesStd) {
  std::error_code ec;
  EXPECT_EQ(rfs::canonical(dir_.path(), ec), fs::canonical(dir_.path()));
  EXPECT_FALSE(ec);
}

TEST_F(FsPassthrough, DirectoryIteratorMatchesStd) {
  std::error_code ec;
  auto it = rfs::directory_iterator(dir_.path(), ec);
  EXPECT_FALSE(ec);
  std::size_t count = 0;
  for (; it != rfs::directory_iterator{}; ++it) ++count;
  EXPECT_EQ(count, 1u);
}

TEST_F(FsPassthrough, IsEmptyMatchesStd) {
  std::error_code ec;
  EXPECT_FALSE(rfs::is_empty(dir_.path(), ec));
  EXPECT_FALSE(ec);
  EXPECT_FALSE(rfs::is_empty(dir_.path() / "missing", ec));
  EXPECT_TRUE(ec);  // matches std: nonexistent path reports an error
}

TEST_F(FsPassthrough, PermissionsMatchesStd) {
  std::error_code ec;
  rfs::permissions(file_, fs::perms::owner_read, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(fs::status(file_).permissions() & fs::perms::owner_write, fs::perms::none);
}

TEST_F(FsPassthrough, SymlinkQueriesMatchStd) {
  const auto link = dir_.path() / "link";
  fs::create_symlink(file_, link);
  std::error_code ec;
  EXPECT_TRUE(rfs::is_symlink(link, ec));
  EXPECT_EQ(rfs::read_symlink(link, ec), file_);
  EXPECT_TRUE(rfs::equivalent(file_, file_, ec));
  EXPECT_FALSE(ec);
}

TEST_F(FsPassthrough, CurrentPathMatchesStd) {
  std::error_code ec;
  EXPECT_EQ(rfs::current_path(ec), fs::current_path());
}

TEST_F(FsPassthrough, CreateDirectoryMatchesStd) {
  std::error_code ec;
  EXPECT_TRUE(rfs::create_directory(dir_.path() / "one", ec));
  EXPECT_TRUE(fs::is_directory(dir_.path() / "one"));
}

TEST_F(FsPassthrough, SpaceReportsCapacity) {
  std::error_code ec;
  EXPECT_GT(rfs::space(dir_.path(), ec).capacity, 0u);
  EXPECT_FALSE(ec);
}

TEST_F(FsPassthrough, RecursiveDirectoryIteratorMatchesStd) {
  fs::create_directories(dir_.path() / "sub");
  write_file(dir_.path() / "sub" / "inner.txt", "x");
  std::error_code ec;
  std::size_t count = 0;
  for (auto it = rfs::recursive_directory_iterator(dir_.path(), ec);
       it != rfs::recursive_directory_iterator{}; ++it) {
    ++count;
  }
  EXPECT_EQ(count, 3u);  // file.txt, sub, sub/inner.txt
}

TEST_F(FsPassthrough, ProximateMatchesStd) {
  std::error_code ec;
  const auto target = dir_.path() / "a" / "b";
  EXPECT_EQ(rfs::proximate(target, dir_.path(), ec), fs::path("a/b"));
}

TEST_F(FsPassthrough, LastWriteTimeMatchesStd) {
  std::error_code ec;
  EXPECT_EQ(rfs::last_write_time(file_, ec), fs::last_write_time(file_));
}

TEST_F(FsPassthrough, RemoveAllCountsRemovedEntries) {
  fs::create_directories(dir_.path() / "tree" / "x");
  write_file(dir_.path() / "tree" / "x" / "f", "1");
  std::error_code ec;
  EXPECT_EQ(rfs::remove_all(dir_.path() / "tree", ec), 3u);
  EXPECT_FALSE(fs::exists(dir_.path() / "tree"));
}

TEST_F(FsPassthrough, ResizeFileMatchesStd) {
  std::error_code ec;
  rfs::resize_file(file_, 2, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(fs::file_size(file_), 2u);
}

TEST_F(FsPassthrough, RemoveFileMatchesStd) {
  std::error_code ec;
  EXPECT_TRUE(rfs::remove(file_, ec));
  EXPECT_FALSE(fs::exists(file_));
}

TEST_F(FsPassthrough, RenameMatchesStd) {
  std::error_code ec;
  const auto dst = dir_.path() / "renamed.txt";
  rfs::rename(file_, dst, ec);
  EXPECT_FALSE(ec);
  EXPECT_TRUE(fs::exists(dst));
  EXPECT_FALSE(fs::exists(file_));
}

// Routing: paths inside a mount must not reach the host. Until archive lookups
// exist, each wrapper reports not_supported.

class FsRouting : public ::testing::Test {
 protected:
  void SetUp() override {
    mounts::clear();
    rfs::cwd::clear();
    ASSERT_TRUE(mounts::add("/rwz"));
  }
  void TearDown() override { mounts::clear();
    rfs::cwd::clear(); }

  const fs::path inside_ = "/rwz/a/b.txt";
  const fs::path other_ = "/rwzfs-test-other/b.txt";
};

TEST_F(FsRouting, EveryWrapperRejectsArchivePath) {
  std::error_code ec;
  EXPECT_FALSE(rfs::exists(inside_, ec));
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  EXPECT_EQ(rfs::status(inside_, ec).type(), fs::file_type::none);
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  rfs::copy(inside_, other_, fs::copy_options::none, ec);
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  rfs::copy(other_, inside_, fs::copy_options::none, ec);
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  EXPECT_FALSE(rfs::create_directories(inside_, ec));
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  EXPECT_FALSE(rfs::is_regular_file(inside_, ec));
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  EXPECT_FALSE(rfs::is_directory(inside_, ec));
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  EXPECT_EQ(rfs::canonical(inside_, ec), fs::path{});
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  (void)rfs::directory_iterator(inside_, ec);
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  EXPECT_FALSE(rfs::is_empty(inside_, ec));
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  rfs::permissions(inside_, fs::perms::owner_read, ec);
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  EXPECT_FALSE(rfs::is_symlink(inside_, ec));
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  EXPECT_FALSE(rfs::equivalent(inside_, other_, ec));
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  EXPECT_EQ(rfs::read_symlink(inside_, ec), fs::path{});
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  EXPECT_FALSE(rfs::create_directory(inside_, ec));
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  (void)rfs::space(inside_, ec);
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  (void)rfs::recursive_directory_iterator(inside_, ec);
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  EXPECT_EQ(rfs::proximate(inside_, other_, ec), fs::path{});
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  (void)rfs::last_write_time(inside_, ec);
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  EXPECT_EQ(rfs::remove_all(inside_, ec), 0u);
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  rfs::resize_file(inside_, 0, ec);
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  EXPECT_FALSE(rfs::remove(inside_, ec));
  EXPECT_EQ(ec, std::errc::not_supported);

  ec.clear();
  rfs::rename(inside_, other_, ec);
  EXPECT_EQ(ec, std::errc::not_supported);
}

TEST_F(FsRouting, OutsidePathsStillPassThrough) {
  TempDir dir;
  const auto file = dir.path() / "f";
  write_file(file, "x");
  std::error_code ec;
  EXPECT_TRUE(rfs::exists(file, ec));
  EXPECT_FALSE(ec);
}

TEST_F(FsRouting, RelativePathsStillPassThrough) {
  rfs::cwd::clear();
  std::error_code ec;
  EXPECT_EQ(rfs::absolute(fs::path("x"), ec), fs::absolute("x"));
  EXPECT_FALSE(ec);
}

TEST_F(FsRouting, AbsoluteIsLexicalForArchivePaths) {
  std::error_code ec;
  EXPECT_EQ(rfs::absolute(fs::path("/rwz/a/../b"), ec),
            fs::path("/rwz/b"));
  EXPECT_FALSE(ec);
}

TEST_F(FsRouting, RelativePathsRouteWhenArchiveCwdIsSet) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd("/rwz/sub"));
  std::error_code ec;
  EXPECT_FALSE(rfs::exists("file.txt", ec));
  EXPECT_EQ(ec, std::errc::not_supported);
}

TEST_F(FsRouting, RelativePathsRouteAfterMount) {
  std::error_code ec;
  EXPECT_FALSE(rfs::exists("DIR/FILE.DAT", ec));
  EXPECT_EQ(ec, std::errc::not_supported);
  EXPECT_EQ(rfs::resolve("DIR/FILE.DAT"), fs::path("/rwz/DIR/FILE.DAT"));
}
