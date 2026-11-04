#include "../support/platform_paths.hpp"
#include <gtest/gtest.h>

#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <string>
#include <unistd.h>

#include "../fs/test_support.hpp"
#include "rwzfs/archive/create.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/filesystem.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/io/posix.hpp"
#include "rwzfs/mount.hpp"
#include "rwzfs/revise.hpp"

namespace fs = std::filesystem;
namespace mounts = rwzfs::fs::mounts;
namespace rfs = rwzfs::fs;
namespace posix = rwzfs::io::posix;

namespace {

// Compression for these tests. The library default needs a zstd that exposes wlog/strat, which
// not every machine's dwarfs build has.
rwzfs::create_options options() {
  rwzfs::create_options o;
  o.compression = "zstd:level=12";
  return o;
}

std::string read_file(const char* path) {
  const int fd = posix::open(path, O_RDONLY, 0);
  if (fd < 0) return "<missing>";
  char buf[64] = {};
  const auto n = posix::read(fd, buf, sizeof(buf));
  posix::close(fd);
  return std::string(buf, n < 0 ? 0 : static_cast<std::size_t>(n));
}

std::set<std::string> names(const char* dir) {
  std::set<std::string> out;
  DIR* d = posix::opendir(dir);
  if (d == nullptr) return out;
  while (const struct dirent* e = posix::readdir(d)) out.insert(e->d_name);
  posix::closedir(d);
  return out;
}

// Source tree for a new archive.
void make_source(const fs::path& src) {
  fs::create_directories(src / "DIR" / "SUB");
  std::ofstream(src / "DIR" / "FILE.DAT") << "payload";
  std::ofstream(src / "DIR" / "SUB" / "inner.txt") << "x";
  std::ofstream(src / "ROOT.TXT") << "root";
}

}  // namespace

class CreateRevise : public ::testing::Test {
 protected:
  void SetUp() override {
    mounts::clear();
    rfs::cwd::clear();
  }
  void TearDown() override {
    mounts::clear();
    rfs::cwd::clear();
  }

  TempDir work_;
};

TEST_F(CreateRevise, CreatedArchiveMountsAndReadsBack) {
  const auto src = work_.path() / "src";
  make_source(src);
  const auto archive = work_.path() / "base.dwarfs";
  ASSERT_TRUE(rwzfs::create_archive(src, archive, options()).has_value());
  EXPECT_FALSE(fs::exists(work_.path() / "base.dwarfs.tmp"));

  TempDir writes;
  rwzfs::mount_options mo;
  mo.write_root = writes.path();
  ASSERT_EQ(rwzfs::mount_archive(archive, RWZ_ROOT "", mo), rwzfs::MountError::None);
  EXPECT_EQ(read_file(RWZ_ROOT "/DIR/FILE.DAT"), "payload");
  EXPECT_EQ(names(RWZ_ROOT ""), (std::set<std::string>{".", "..", "DIR", "ROOT.TXT"}));
}

TEST_F(CreateRevise, RevisionFlattensOverlayIntoNextArchive) {
  const auto src = work_.path() / "src";
  make_source(src);
  const auto archive = work_.path() / "base.dwarfs";
  ASSERT_TRUE(rwzfs::create_archive(src, archive, options()).has_value());

  TempDir writes;
  rwzfs::mount_options mo;
  mo.write_root = writes.path();
  ASSERT_EQ(rwzfs::mount_archive(archive, RWZ_ROOT "", mo), rwzfs::MountError::None);

  // Changes: overwrite the first bytes, delete a root file, create a new file.
  const int fd = posix::open(RWZ_ROOT "/DIR/FILE.DAT", O_WRONLY, 0);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(posix::write(fd, "PAY", 3), 3);
  posix::close(fd);
  ASSERT_EQ(posix::unlink(RWZ_ROOT "/ROOT.TXT"), 0);
  const int nfd = posix::open(RWZ_ROOT "/NEW.TXT", O_WRONLY | O_CREAT, 0644);
  ASSERT_GE(nfd, 0);
  posix::write(nfd, "new", 3);
  posix::close(nfd);

  const auto revised = rwzfs::revise_archive(RWZ_ROOT "", options());
  ASSERT_TRUE(revised.has_value());
  EXPECT_EQ(revised->filename(), "base-rev1.dwarfs");
  EXPECT_TRUE(fs::exists(*revised));
  EXPECT_FALSE(fs::exists(writes.path() / "base-writes" / "data"));  // overlay cleared
  std::error_code ec;
  EXPECT_FALSE(rfs::exists(fs::path(RWZ_ROOT "/DIR/FILE.DAT"), ec));      // old mount is gone

  // Mount the revision with a fresh overlay: its content is the merged view of the old one.
  TempDir writes2;
  rwzfs::mount_options mo2;
  mo2.write_root = writes2.path();
  ASSERT_EQ(rwzfs::mount_archive(*revised, RWZ_ROOT "", mo2), rwzfs::MountError::None);
  EXPECT_EQ(read_file(RWZ_ROOT "/DIR/FILE.DAT"), "PAYload");
  EXPECT_EQ(read_file(RWZ_ROOT "/NEW.TXT"), "new");
  EXPECT_EQ(read_file(RWZ_ROOT "/ROOT.TXT"), "<missing>");
  EXPECT_EQ(names(RWZ_ROOT "/DIR/SUB"), (std::set<std::string>{".", "..", "inner.txt"}));
}

TEST_F(CreateRevise, RevisingARevisionBumpsTheNumber) {
  const auto src = work_.path() / "src";
  make_source(src);
  const auto archive = work_.path() / "base.dwarfs";
  ASSERT_TRUE(rwzfs::create_archive(src, archive, options()).has_value());

  TempDir w1;
  rwzfs::mount_options m1;
  m1.write_root = w1.path();
  ASSERT_EQ(rwzfs::mount_archive(archive, RWZ_ROOT "", m1), rwzfs::MountError::None);
  const int fd = posix::open(RWZ_ROOT "/NEW1.TXT", O_WRONLY | O_CREAT, 0644);
  ASSERT_GE(fd, 0);
  posix::close(fd);
  const auto rev1 = rwzfs::revise_archive(RWZ_ROOT "", options());
  ASSERT_TRUE(rev1.has_value());

  TempDir w2;
  rwzfs::mount_options m2;
  m2.write_root = w2.path();
  ASSERT_EQ(rwzfs::mount_archive(*rev1, RWZ_ROOT "", m2), rwzfs::MountError::None);
  const int fd2 = posix::open(RWZ_ROOT "/NEW2.TXT", O_WRONLY | O_CREAT, 0644);
  ASSERT_GE(fd2, 0);
  posix::close(fd2);
  const auto rev2 = rwzfs::revise_archive(RWZ_ROOT "", options());
  ASSERT_TRUE(rev2.has_value());
  EXPECT_EQ(rev2->filename(), "base-rev2.dwarfs");
}

TEST_F(CreateRevise, RevisionWithoutChangesIsRefused) {
  const auto src = work_.path() / "src";
  make_source(src);
  const auto archive = work_.path() / "base.dwarfs";
  ASSERT_TRUE(rwzfs::create_archive(src, archive, options()).has_value());
  TempDir writes;
  rwzfs::mount_options mo;
  mo.write_root = writes.path();
  ASSERT_EQ(rwzfs::mount_archive(archive, RWZ_ROOT "", mo), rwzfs::MountError::None);
  const auto result = rwzfs::revise_archive(RWZ_ROOT "", options());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), rwzfs::revise_error::no_changes);
  EXPECT_TRUE(fs::exists(archive));  // still mounted and untouched
}

TEST_F(CreateRevise, RevisingAnUnmountedPathIsRefused) {
  const auto result = rwzfs::revise_archive(RWZ_ROOT "", options());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), rwzfs::revise_error::mount_not_found);
}

TEST_F(CreateRevise, CreatedArchiveRemovesTempFileOnToolFailure) {
  const auto src = work_.path() / "src";
  make_source(src);
  rwzfs::create_options bad;
  bad.compression = "zstd:no-such-option=1";
  const auto out = work_.path() / "bad.dwarfs";
  const auto result = rwzfs::create_archive(src, out, bad);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), rwzfs::create_error::tool_failed);
  EXPECT_FALSE(fs::exists(out));
  EXPECT_FALSE(fs::exists(work_.path() / "bad.dwarfs.tmp"));
}
