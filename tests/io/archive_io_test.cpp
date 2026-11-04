#if !defined(_WIN32)

#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <climits>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <glob.h>
#include <sys/ioctl.h>
#include <memory>
#include <set>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../support/fake_backend.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/io/posix.hpp"

namespace posix = rwzfs::io::posix;
namespace mounts = rwzfs::fs::mounts;
namespace rfs = rwzfs::fs;

namespace {

constexpr const char* kFile = "/rwz/DIR/FILE.DAT";
constexpr const char* kDir = "/rwz/DIR";
constexpr const char* kPayload = "payload";
constexpr std::size_t kPayloadSize = 7;
constexpr std::size_t kBuf = 64;
constexpr std::size_t kPathBuf = PATH_MAX;

std::string read_all(int fd, std::size_t n) {
  std::string out(n, '\0');
  const auto got = posix::read(fd, out.data(), n);
  out.resize(got < 0 ? 0 : static_cast<std::size_t>(got));
  return out;
}

}  // namespace

class ArchiveIo : public ::testing::Test {
 protected:
  void SetUp() override {
    mounts::clear();
    rfs::cwd::clear();
    auto backend = std::make_shared<fake_backend>();
    backend->add_file("DIR/FILE.DAT", kPayload);
    backend->add_dir("EMPTY");
    backend_ = backend;
    ASSERT_TRUE(mounts::add("/rwz", backend_));
  }
  void TearDown() override {
    mounts::clear();
    rfs::cwd::clear();
  }

  std::shared_ptr<const rwzfs::archive::backend> backend_;
};

// Descriptors

TEST_F(ArchiveIo, OpenReadSeekCloseRoundTrip) {
  const int fd = posix::open(kFile, O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(read_all(fd, kBuf), kPayload);
  EXPECT_EQ(posix::lseek(fd, 0, SEEK_SET), 0);
  EXPECT_EQ(read_all(fd, kBuf), kPayload);
  EXPECT_EQ(posix::close(fd), 0);
}

TEST_F(ArchiveIo, ReadAtEndReturnsZero) {
  const int fd = posix::open(kFile, O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(read_all(fd, kBuf), kPayload);
  EXPECT_EQ(read_all(fd, kBuf), "");
  posix::close(fd);
}

TEST_F(ArchiveIo, SeekWhenceValues) {
  const int fd = posix::open(kFile, O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(posix::lseek(fd, 3, SEEK_SET), 3);
  EXPECT_EQ(posix::lseek(fd, 1, SEEK_CUR), 4);
  EXPECT_EQ(posix::lseek(fd, -2, SEEK_END), static_cast<off_t>(kPayloadSize) - 2);
  EXPECT_EQ(read_all(fd, kBuf), "ad");
  posix::close(fd);
}

TEST_F(ArchiveIo, SeekBeforeStartFailsWithEinval) {
  const int fd = posix::open(kFile, O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  errno = 0;
  EXPECT_EQ(posix::lseek(fd, -1, SEEK_SET), -1);
  EXPECT_EQ(errno, EINVAL);
  posix::close(fd);
}

TEST_F(ArchiveIo, RelativeOpenUnderArchiveCwd) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd("/rwz/DIR"));
  const int fd = posix::open("FILE.DAT", O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(read_all(fd, kBuf), kPayload);
  posix::close(fd);
}

TEST_F(ArchiveIo, OpenMissingFileFailsWithEnoent) {
  errno = 0;
  EXPECT_EQ(posix::open("/rwz/nope", O_RDONLY, 0), -1);
  EXPECT_EQ(errno, ENOENT);
}

TEST_F(ArchiveIo, OpenDirectoryFailsWithEisdir) {
  errno = 0;
  EXPECT_EQ(posix::open(kDir, O_RDONLY, 0), -1);
  EXPECT_EQ(errno, EISDIR);
}

TEST_F(ArchiveIo, OpenForWritingFailsWithErofs) {
  errno = 0;
  EXPECT_EQ(posix::open(kFile, O_WRONLY, 0), -1);
  EXPECT_EQ(errno, EROFS);
  errno = 0;
  EXPECT_EQ(posix::open("/rwz/new.txt", O_RDWR | O_CREAT, 0644), -1);
  EXPECT_EQ(errno, EROFS);
}

TEST_F(ArchiveIo, WriteToArchiveDescriptorFailsWithEbadf) {
  const int fd = posix::open(kFile, O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  errno = 0;
  EXPECT_EQ(posix::write(fd, "x", 1), -1);
  EXPECT_EQ(errno, EBADF);
  posix::close(fd);
}

TEST_F(ArchiveIo, CloseTwiceFailsWithEbadf) {
  const int fd = posix::open(kFile, O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(posix::close(fd), 0);
  errno = 0;
  EXPECT_EQ(posix::close(fd), -1);
  EXPECT_EQ(errno, EBADF);
}

TEST_F(ArchiveIo, FsyncOnArchiveDescriptorSucceeds) {
  const int fd = posix::open(kFile, O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(posix::fsync(fd), 0);
  posix::close(fd);
}

TEST_F(ArchiveIo, IoctlAndMmapRejectArchiveDescriptors) {
  const int fd = posix::open(kFile, O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  int n = 0;
  errno = 0;
  EXPECT_EQ(posix::ioctl(fd, FIONREAD, &n), -1);
  EXPECT_EQ(errno, ENOTTY);
  errno = 0;
  EXPECT_EQ(posix::mmap(nullptr, kPayloadSize, PROT_READ, MAP_PRIVATE, fd, 0), MAP_FAILED);
  EXPECT_EQ(errno, ENODEV);
  posix::close(fd);
}

// Paths

TEST_F(ArchiveIo, StatFileReportsModeAndSize) {
  struct stat st{};
  ASSERT_EQ(posix::stat(kFile, &st), 0);
  EXPECT_TRUE(S_ISREG(st.st_mode));
  EXPECT_EQ(st.st_size, static_cast<off_t>(kPayloadSize));
}

TEST_F(ArchiveIo, StatDirectoryReportsDirectoryMode) {
  struct stat st{};
  ASSERT_EQ(posix::stat(kDir, &st), 0);
  EXPECT_TRUE(S_ISDIR(st.st_mode));
}

TEST_F(ArchiveIo, StatMissingFailsWithEnoent) {
  struct stat st{};
  errno = 0;
  EXPECT_EQ(posix::stat("/rwz/nope", &st), -1);
  EXPECT_EQ(errno, ENOENT);
}

TEST_F(ArchiveIo, AccessExistenceAndWrite) {
  EXPECT_EQ(posix::access(kFile, F_OK), 0);
  errno = 0;
  EXPECT_EQ(posix::access(kFile, W_OK), -1);
  EXPECT_EQ(errno, EROFS);
  errno = 0;
  EXPECT_EQ(posix::access("/rwz/nope", F_OK), -1);
  EXPECT_EQ(errno, ENOENT);
}

TEST_F(ArchiveIo, MutatingPathCallsFailWithErofs) {
  errno = 0;
  EXPECT_EQ(posix::unlink(kFile), -1);
  EXPECT_EQ(errno, EROFS);
  errno = 0;
  EXPECT_EQ(posix::mkdir("/rwz/new", 0755), -1);
  EXPECT_EQ(errno, EROFS);
  errno = 0;
  EXPECT_EQ(posix::rmdir(kDir), -1);
  EXPECT_EQ(errno, EROFS);
  errno = 0;
  EXPECT_EQ(posix::truncate(kFile, 0), -1);
  EXPECT_EQ(errno, EROFS);
}

TEST_F(ArchiveIo, RealpathReturnsArchivePath) {
  std::array<char, kPathBuf> buf{};
  ASSERT_NE(posix::realpath(kFile, buf.data()), nullptr);
  EXPECT_STREQ(buf.data(), kFile);
}

TEST_F(ArchiveIo, RealpathWithNullBufferAllocates) {
  char* p = posix::realpath(kFile, nullptr);
  ASSERT_NE(p, nullptr);
  EXPECT_STREQ(p, kFile);
  std::free(p);
}

TEST_F(ArchiveIo, RealpathMissingFailsWithEnoent) {
  std::array<char, kPathBuf> buf{};
  errno = 0;
  EXPECT_EQ(posix::realpath("/rwz/nope", buf.data()), nullptr);
  EXPECT_EQ(errno, ENOENT);
}

// Directories

TEST_F(ArchiveIo, ReaddirListsDotEntriesAndChildren) {
  DIR* dir = posix::opendir("/rwz");
  ASSERT_NE(dir, nullptr);
  std::set<std::string> names;
  while (const struct dirent* e = posix::readdir(dir)) {
    names.insert(e->d_name);
  }
  EXPECT_EQ(names, (std::set<std::string>{".", "..", "DIR", "EMPTY"}));
  EXPECT_EQ(posix::closedir(dir), 0);
}

TEST_F(ArchiveIo, OpendirOnFileFailsWithEnotdir) {
  errno = 0;
  EXPECT_EQ(posix::opendir(kFile), nullptr);
  EXPECT_EQ(errno, ENOTDIR);
}

TEST_F(ArchiveIo, OpendirMissingFailsWithEnoent) {
  errno = 0;
  EXPECT_EQ(posix::opendir("/rwz/nope"), nullptr);
  EXPECT_EQ(errno, ENOENT);
}

TEST_F(ArchiveIo, GlobMatchesSortedArchivePaths) {
  glob_t matches{};
  ASSERT_EQ(posix::glob("/rwz/*/*.DAT", 0, nullptr, &matches), 0);
  ASSERT_EQ(matches.gl_pathc, 1u);
  EXPECT_STREQ(matches.gl_pathv[0], "/rwz/DIR/FILE.DAT");
  globfree(&matches);
}

TEST_F(ArchiveIo, GlobNoMatchReturnsNomatch) {
  glob_t matches{};
  EXPECT_EQ(posix::glob("/rwz/nope*", 0, nullptr, &matches), GLOB_NOMATCH);
  globfree(&matches);
}

TEST_F(ArchiveIo, GlobAppendIsNotSupported) {
  glob_t matches{};
  errno = 0;
  EXPECT_EQ(posix::glob("/rwz/*", GLOB_APPEND, nullptr, &matches), GLOB_ABORTED);
  EXPECT_EQ(errno, ENOTSUP);
}

// Working directory

TEST_F(ArchiveIo, ChdirIntoArchiveDirectorySucceeds) {
  EXPECT_EQ(posix::chdir(kDir), 0);
  EXPECT_EQ(rfs::cwd::archive_cwd(), std::filesystem::path(kDir));
  std::array<char, kBuf> buf{};
  ASSERT_NE(posix::getcwd(buf.data(), buf.size()), nullptr);
  EXPECT_STREQ(buf.data(), kDir);
}

TEST_F(ArchiveIo, ChdirIntoFileFailsWithEnotdir) {
  errno = 0;
  EXPECT_EQ(posix::chdir(kFile), -1);
  EXPECT_EQ(errno, ENOTDIR);
  EXPECT_EQ(rfs::cwd::archive_cwd(), std::filesystem::path("/rwz"));
}

TEST_F(ArchiveIo, ChdirMissingFailsWithEnoent) {
  errno = 0;
  EXPECT_EQ(posix::chdir("/rwz/nope"), -1);
  EXPECT_EQ(errno, ENOENT);
}

#endif  // !defined(_WIN32)
