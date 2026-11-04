#if !defined(_WIN32)

#include "../support/platform_paths.hpp"
#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fcntl.h>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <unistd.h>

#include "../fs/test_support.hpp"
#include "../support/fake_backend.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/filesystem.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/io/posix.hpp"
#include "rwzfs/io/stdio.hpp"

namespace fs = std::filesystem;
namespace rfs = rwzfs::fs;
namespace mounts = rwzfs::fs::mounts;
namespace posix = rwzfs::io::posix;
namespace stdio = rwzfs::io::stdio;

namespace {

std::string read_fd(int fd, std::size_t n) {
  std::string out(n, '\0');
  const auto got = posix::read(fd, out.data(), n);
  out.resize(got < 0 ? 0 : static_cast<std::size_t>(got));
  return out;
}

std::set<std::string> names(const char* dir) {
  std::set<std::string> out;
  DIR* d = posix::opendir(dir);
  if (d == nullptr) return out;
  while (const struct dirent* e = posix::readdir(d)) out.insert(e->d_name);
  posix::closedir(d);
  return out;
}

}  // namespace

class OverlayIntegration : public ::testing::Test {
 protected:
  void SetUp() override {
    mounts::clear();
    rfs::cwd::clear();
    backend_ = std::make_shared<fake_backend>();
    backend_->add_file("DIR/FILE.DAT", "payload");
    backend_->add_file("DIR/SUB/inner.txt", "x");
    backend_->add_file("ROOT.TXT", "root");
    ASSERT_TRUE(mounts::add(RWZ_ROOT "", backend_, overlay_.path()));
  }
  void TearDown() override {
    mounts::clear();
    rfs::cwd::clear();
  }

  // The archive must never change: its entries and content stay as they were.
  void expect_archive_unchanged() {
    const auto f = backend_->lookup("DIR/FILE.DAT");
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->size, 7u);
    std::error_code ec;
    char buf[16] = {};
    EXPECT_EQ(backend_->read(f->inode, buf, sizeof(buf), 0, ec), 7u);
    EXPECT_EQ(std::string(buf, 7), "payload");
  }

  TempDir overlay_;
  std::shared_ptr<fake_backend> backend_;
};

TEST_F(OverlayIntegration, WriteToArchiveFileCopiesUpAndLeavesArchiveAlone) {
  const int fd = posix::open(RWZ_ROOT "/DIR/FILE.DAT", O_WRONLY, 0);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(posix::write(fd, "PAY", 3), 3);
  posix::close(fd);

  const int rd = posix::open(RWZ_ROOT "/DIR/FILE.DAT", O_RDONLY, 0);
  ASSERT_GE(rd, 0);
  EXPECT_EQ(read_fd(rd, 16), "PAYload");
  posix::close(rd);
  expect_archive_unchanged();
}

TEST_F(OverlayIntegration, CreateNewFileIsVisibleInListing) {
  const int fd = posix::open(RWZ_ROOT "/DIR/NEW.TXT", O_WRONLY | O_CREAT, 0644);
  ASSERT_GE(fd, 0);
  posix::write(fd, "new", 3);
  posix::close(fd);
  EXPECT_EQ(names(RWZ_ROOT "/DIR"), (std::set<std::string>{".", "..", "FILE.DAT", "NEW.TXT", "SUB"}));
  expect_archive_unchanged();
}

TEST_F(OverlayIntegration, ExclusiveCreateOfArchiveFileFails) {
  errno = 0;
  EXPECT_EQ(posix::open(RWZ_ROOT "/DIR/FILE.DAT", O_WRONLY | O_CREAT | O_EXCL, 0644), -1);
  EXPECT_EQ(errno, EEXIST);
}

TEST_F(OverlayIntegration, UnlinkHidesArchiveFileAndKeepsArchive) {
  EXPECT_EQ(posix::unlink(RWZ_ROOT "/DIR/FILE.DAT"), 0);
  struct stat st{};
  errno = 0;
  EXPECT_EQ(posix::stat(RWZ_ROOT "/DIR/FILE.DAT", &st), -1);
  EXPECT_EQ(errno, ENOENT);
  EXPECT_EQ(names(RWZ_ROOT "/DIR"), (std::set<std::string>{".", "..", "SUB"}));
  expect_archive_unchanged();
}

TEST_F(OverlayIntegration, TruncateArchiveFileCopiesUp) {
  EXPECT_EQ(posix::truncate(RWZ_ROOT "/DIR/FILE.DAT", 3), 0);
  struct stat st{};
  ASSERT_EQ(posix::stat(RWZ_ROOT "/DIR/FILE.DAT", &st), 0);
  EXPECT_EQ(st.st_size, 3);
  expect_archive_unchanged();
}

TEST_F(OverlayIntegration, MkdirRmdirRoundTrip) {
  EXPECT_EQ(posix::mkdir(RWZ_ROOT "/NEWDIR", 0755), 0);
  EXPECT_EQ(names(RWZ_ROOT ""), (std::set<std::string>{".", "..", "DIR", "NEWDIR", "ROOT.TXT"}));
  EXPECT_EQ(posix::rmdir(RWZ_ROOT "/NEWDIR"), 0);
  errno = 0;
  EXPECT_EQ(posix::mkdir(RWZ_ROOT "/DIR", 0755), -1);
  EXPECT_EQ(errno, EEXIST);
}

TEST_F(OverlayIntegration, RmdirOfNonEmptyDirectoryFails) {
  errno = 0;
  EXPECT_EQ(posix::rmdir(RWZ_ROOT "/DIR"), -1);
  EXPECT_EQ(errno, ENOTEMPTY);
}

TEST_F(OverlayIntegration, AccessAndStatOfOverlayFile) {
  const int fd = posix::open(RWZ_ROOT "/DIR/NEW.TXT", O_WRONLY | O_CREAT, 0644);
  ASSERT_GE(fd, 0);
  posix::write(fd, "abcd", 4);
  posix::close(fd);
  struct stat st{};
  ASSERT_EQ(posix::stat(RWZ_ROOT "/DIR/NEW.TXT", &st), 0);
  EXPECT_EQ(st.st_size, 4);
  EXPECT_EQ(posix::access(RWZ_ROOT "/DIR/NEW.TXT", F_OK), 0);
}

TEST_F(OverlayIntegration, RenameArchiveFileThroughStdio) {
  EXPECT_EQ(stdio::rename(RWZ_ROOT "/ROOT.TXT", RWZ_ROOT "/MOVED.TXT"), 0);
  EXPECT_EQ(names(RWZ_ROOT ""), (std::set<std::string>{".", "..", "DIR", "MOVED.TXT"}));
  std::FILE* fp = stdio::fopen(RWZ_ROOT "/MOVED.TXT", "rb");
  ASSERT_NE(fp, nullptr);
  char buf[8] = {};
  EXPECT_EQ(std::fread(buf, 1, sizeof(buf), fp), 4u);
  EXPECT_EQ(std::string(buf, 4), "root");
  stdio::fclose(fp);
  expect_archive_unchanged();
}

TEST_F(OverlayIntegration, StdioWriteModeCreatesOverlayFile) {
  std::FILE* fp = stdio::fopen(RWZ_ROOT "/DIR/WRITTEN.TXT", "w");
  ASSERT_NE(fp, nullptr);
  std::fputs("hello", fp);
  EXPECT_EQ(stdio::fclose(fp), 0);

  std::FILE* rd = stdio::fopen(RWZ_ROOT "/DIR/WRITTEN.TXT", "rb");
  ASSERT_NE(rd, nullptr);
  char buf[8] = {};
  EXPECT_EQ(std::fread(buf, 1, sizeof(buf), rd), 5u);
  stdio::fclose(rd);
}

TEST_F(OverlayIntegration, StdioRemoveOfArchiveFileWhiteouts) {
  EXPECT_EQ(stdio::remove(RWZ_ROOT "/ROOT.TXT"), 0);
  EXPECT_EQ(stdio::fopen(RWZ_ROOT "/ROOT.TXT", "rb"), nullptr);
  EXPECT_EQ(errno, ENOENT);
}

TEST_F(OverlayIntegration, FsCreateDirectoriesAndExists) {
  std::error_code ec;
  EXPECT_TRUE(rfs::create_directories(fs::path(RWZ_ROOT "/NEW/A/B"), ec));
  EXPECT_FALSE(ec);
  EXPECT_TRUE(rfs::is_directory(fs::path(RWZ_ROOT "/NEW/A/B"), ec));
  EXPECT_FALSE(rfs::create_directories(fs::path(RWZ_ROOT "/NEW/A/B"), ec));
  EXPECT_FALSE(ec);
}

TEST_F(OverlayIntegration, FsRemoveAllCountsMergedEntries) {
  std::error_code ec;
  // DIR, DIR/FILE.DAT, DIR/SUB, DIR/SUB/inner.txt
  EXPECT_EQ(rfs::remove_all(fs::path(RWZ_ROOT "/DIR"), ec), 4u);
  EXPECT_FALSE(ec);
  EXPECT_FALSE(rfs::exists(fs::path(RWZ_ROOT "/DIR"), ec));
  expect_archive_unchanged();
}

TEST_F(OverlayIntegration, FsRenameAndRemoveThroughOverlay) {
  std::error_code ec;
  rfs::rename(fs::path(RWZ_ROOT "/ROOT.TXT"), fs::path(RWZ_ROOT "/R2.TXT"), ec);
  EXPECT_FALSE(ec);
  EXPECT_TRUE(rfs::exists(fs::path(RWZ_ROOT "/R2.TXT"), ec));
  EXPECT_FALSE(rfs::exists(fs::path(RWZ_ROOT "/ROOT.TXT"), ec));
  EXPECT_TRUE(rfs::remove(fs::path(RWZ_ROOT "/R2.TXT"), ec));
  EXPECT_FALSE(rfs::exists(fs::path(RWZ_ROOT "/R2.TXT"), ec));
}

TEST_F(OverlayIntegration, FsPermissionsCopyUpThroughOverlay) {
  std::error_code ec;
  rfs::permissions(fs::path(RWZ_ROOT "/DIR/FILE.DAT"), fs::perms::owner_read, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(rfs::status(fs::path(RWZ_ROOT "/DIR/FILE.DAT"), ec).permissions() & fs::perms::owner_write,
            fs::perms::none);
  expect_archive_unchanged();
}

TEST_F(OverlayIntegration, RelativeWritesUseArchiveCwd) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd(RWZ_ROOT "/DIR"));
  const int fd = posix::open("LOCAL.TXT", O_WRONLY | O_CREAT, 0644);
  ASSERT_GE(fd, 0);
  posix::close(fd);
  EXPECT_TRUE(fs::exists(overlay_.path() / "data" / "DIR" / "LOCAL.TXT"));
}

TEST_F(OverlayIntegration, WritesUnderMissingDirectoryFail) {
  errno = 0;
  EXPECT_EQ(posix::open(RWZ_ROOT "/NODIR/x", O_WRONLY | O_CREAT, 0644), -1);
  EXPECT_EQ(errno, ENOENT);
}

#endif  // !defined(_WIN32)
