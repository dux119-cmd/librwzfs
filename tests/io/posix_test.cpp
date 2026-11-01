#if !defined(_WIN32)

#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <glob.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/io/posix.hpp"
#include "../fs/test_support.hpp"

namespace fs = std::filesystem;
namespace posix = rwzfs::io::posix;
namespace mounts = rwzfs::fs::mounts;
namespace rfs = rwzfs::fs;

namespace {

constexpr std::size_t kReadBuf = 16;
constexpr std::size_t kTinyBuf = 4;
constexpr std::size_t kPathBuf = 4096;
constexpr std::size_t kHelloSize = 5;
constexpr const char* kArchiveFile = "/rwz/a.txt";
constexpr const char* kArchiveDir = "/rwz/sub";

std::string read_fd(int fd, std::size_t n) {
  std::string out(n, '\0');
  const auto got = posix::read(fd, out.data(), n);
  out.resize(got < 0 ? 0 : static_cast<std::size_t>(got));
  return out;
}

}  // namespace

class PosixIo : public ::testing::Test {
 protected:
  void SetUp() override {
    original_ = fs::current_path();
    mounts::clear();
    rfs::cwd::clear();
    ASSERT_TRUE(mounts::add("/rwz"));
    file_ = dir_.path() / "file.txt";
    std::ofstream(file_) << "hello";
  }
  void TearDown() override {
    rfs::cwd::clear();
    mounts::clear();
    std::error_code ec;
    fs::current_path(original_, ec);
  }

  fs::path original_;
  TempDir dir_;
  fs::path file_;
};

// Descriptors (POSIX-01, 02, 03, 07, 18)

TEST_F(PosixIo, OpenReadCloseRoundTrip) {
  const int fd = posix::open(file_.c_str(), O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(read_fd(fd, kReadBuf), "hello");
  EXPECT_EQ(posix::close(fd), 0);
}

TEST_F(PosixIo, SeekRewindsForRereading) {
  const int fd = posix::open(file_.c_str(), O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(read_fd(fd, kReadBuf), "hello");
  EXPECT_EQ(posix::lseek(fd, 0, SEEK_SET), 0);
  EXPECT_EQ(read_fd(fd, kReadBuf), "hello");
  posix::close(fd);
}

TEST_F(PosixIo, WriteThenReadBack) {
  const auto path = dir_.path() / "out.txt";
  const int fd = posix::open(path.c_str(), O_RDWR | O_CREAT, 0600);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(posix::write(fd, "abc", 3), 3);
  EXPECT_EQ(posix::lseek(fd, 0, SEEK_SET), 0);
  EXPECT_EQ(read_fd(fd, kReadBuf), "abc");
  EXPECT_EQ(posix::fsync(fd), 0);
  posix::close(fd);
}

TEST_F(PosixIo, CloseBadDescriptorFailsWithEbadf) {
  errno = 0;
  EXPECT_EQ(posix::close(-1), -1);
  EXPECT_EQ(errno, EBADF);
}

TEST_F(PosixIo, IoctlFionreadReportsRemainingBytes) {
  const int fd = posix::open(file_.c_str(), O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  int remaining = -1;
  EXPECT_EQ(posix::ioctl(fd, FIONREAD, &remaining), 0);
  EXPECT_EQ(remaining, static_cast<int>(kHelloSize));
  posix::close(fd);
}

TEST_F(PosixIo, MmapHostFileReadsContent) {
  const int fd = posix::open(file_.c_str(), O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  void* mapped = posix::mmap(nullptr, kHelloSize, PROT_READ, MAP_PRIVATE, fd, 0);
  ASSERT_NE(mapped, MAP_FAILED);
  EXPECT_EQ(std::string(static_cast<const char*>(mapped), kHelloSize), "hello");
  ::munmap(mapped, kHelloSize);
  posix::close(fd);
}

// Paths (POSIX-04, 05, 08, 10, 14-17, 20). Host paths pass; archive paths fail ENOTSUP.

TEST_F(PosixIo, OpenArchivePathFailsWithEnotsup) {
  errno = 0;
  EXPECT_EQ(posix::open(kArchiveFile, O_RDONLY, 0), -1);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(PosixIo, OpenRelativeUnderArchiveCwdFailsWithEnotsup) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd(kArchiveDir));
  errno = 0;
  EXPECT_EQ(posix::open("x.txt", O_RDONLY, 0), -1);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(PosixIo, StatHostFileReportsSize) {
  struct stat st{};
  EXPECT_EQ(posix::stat(file_.c_str(), &st), 0);
  EXPECT_EQ(st.st_size, static_cast<off_t>(kHelloSize));
}

TEST_F(PosixIo, StatArchivePathFailsWithEnotsup) {
  struct stat st{};
  errno = 0;
  EXPECT_EQ(posix::stat(kArchiveFile, &st), -1);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(PosixIo, StatRelativeUnderArchiveCwdFailsWithEnotsup) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd(kArchiveDir));
  struct stat st{};
  errno = 0;
  EXPECT_EQ(posix::stat("file.txt", &st), -1);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(PosixIo, UnlinkHostFileRemovesIt) {
  EXPECT_EQ(posix::unlink(file_.c_str()), 0);
  EXPECT_FALSE(fs::exists(file_));
}

TEST_F(PosixIo, UnlinkArchivePathFailsWithEnotsup) {
  errno = 0;
  EXPECT_EQ(posix::unlink(kArchiveFile), -1);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(PosixIo, MkdirAndRmdirHostDirectory) {
  const auto sub = dir_.path() / "sub";
  EXPECT_EQ(posix::mkdir(sub.c_str(), 0700), 0);
  EXPECT_TRUE(fs::is_directory(sub));
  EXPECT_EQ(posix::rmdir(sub.c_str()), 0);
  EXPECT_FALSE(fs::exists(sub));
}

TEST_F(PosixIo, MkdirArchivePathFailsWithEnotsup) {
  errno = 0;
  EXPECT_EQ(posix::mkdir("/rwz/new", 0700), -1);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(PosixIo, RmdirArchivePathFailsWithEnotsup) {
  errno = 0;
  EXPECT_EQ(posix::rmdir(kArchiveDir), -1);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(PosixIo, TruncateHostFile) {
  EXPECT_EQ(posix::truncate(file_.c_str(), 2), 0);
  EXPECT_EQ(fs::file_size(file_), 2u);
}

TEST_F(PosixIo, TruncateArchivePathFailsWithEnotsup) {
  errno = 0;
  EXPECT_EQ(posix::truncate(kArchiveFile, 0), -1);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(PosixIo, AccessHostFileSucceeds) {
  EXPECT_EQ(posix::access(file_.c_str(), F_OK), 0);
}

TEST_F(PosixIo, AccessMissingHostFileFailsWithEnoent) {
  errno = 0;
  EXPECT_EQ(posix::access((dir_.path() / "missing").c_str(), F_OK), -1);
  EXPECT_EQ(errno, ENOENT);
}

TEST_F(PosixIo, OpendirReadsEntriesThenCloses) {
  DIR* dir = posix::opendir(dir_.path().c_str());
  ASSERT_NE(dir, nullptr);
  bool found = false;
  while (const struct dirent* entry = posix::readdir(dir)) {
    if (std::strcmp(entry->d_name, "file.txt") == 0) found = true;
  }
  EXPECT_TRUE(found);
  EXPECT_EQ(posix::closedir(dir), 0);
}

TEST_F(PosixIo, OpendirArchivePathFailsWithEnotsup) {
  errno = 0;
  EXPECT_EQ(posix::opendir("/rwz"), nullptr);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(PosixIo, RealpathHostPathMatchesCanonical) {
  std::array<char, kPathBuf> buf{};
  ASSERT_NE(posix::realpath(file_.c_str(), buf.data()), nullptr);
  EXPECT_EQ(fs::path(buf.data()), fs::canonical(file_));
}

TEST_F(PosixIo, RealpathArchivePathFailsWithEnotsup) {
  std::array<char, kPathBuf> buf{};
  errno = 0;
  EXPECT_EQ(posix::realpath(kArchiveFile, buf.data()), nullptr);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(PosixIo, GlobHostPatternMatches) {
  std::ofstream(dir_.path() / "b.log") << "x";
  const auto pattern = (dir_.path() / "*.txt").string();
  glob_t matches{};
  ASSERT_EQ(posix::glob(pattern.c_str(), 0, nullptr, &matches), 0);
  EXPECT_EQ(matches.gl_pathc, 1u);
  globfree(&matches);
}

TEST_F(PosixIo, GlobArchivePatternAbortsWithEnotsup) {
  glob_t matches{};
  errno = 0;
  EXPECT_EQ(posix::glob("/rwz/*.txt", 0, nullptr, &matches), GLOB_ABORTED);
  EXPECT_EQ(errno, ENOTSUP);
}

// Working directory (CWD-03, CWD-04)

TEST_F(PosixIo, GetcwdPassesThroughWithoutArchiveCwd) {
  rfs::cwd::clear();
  std::array<char, kPathBuf> buf{};
  ASSERT_NE(posix::getcwd(buf.data(), buf.size()), nullptr);
  EXPECT_EQ(fs::path(buf.data()), fs::current_path());
}

TEST_F(PosixIo, GetcwdReturnsArchiveCwd) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd(kArchiveDir));
  std::array<char, 64> buf{};
  ASSERT_NE(posix::getcwd(buf.data(), buf.size()), nullptr);
  EXPECT_STREQ(buf.data(), kArchiveDir);
}

TEST_F(PosixIo, GetcwdTooSmallFailsWithErange) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd(kArchiveDir));
  std::array<char, kTinyBuf> buf{};
  errno = 0;
  EXPECT_EQ(posix::getcwd(buf.data(), buf.size()), nullptr);
  EXPECT_EQ(errno, ERANGE);
}

TEST_F(PosixIo, GetcwdNullBufferWithArchiveCwdFailsWithEinval) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd(kArchiveDir));
  errno = 0;
  EXPECT_EQ(posix::getcwd(nullptr, 0), nullptr);
  EXPECT_EQ(errno, EINVAL);
}

TEST_F(PosixIo, ChdirIntoArchiveFailsWithEnotsup) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd(kArchiveDir));
  errno = 0;
  EXPECT_EQ(posix::chdir("/rwz/other"), -1);
  EXPECT_EQ(errno, ENOTSUP);
  EXPECT_EQ(rfs::cwd::archive_cwd(), fs::path(kArchiveDir));
}

TEST_F(PosixIo, ChdirToHostSucceedsAndClearsArchiveCwd) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd(kArchiveDir));
  EXPECT_EQ(posix::chdir(dir_.path().c_str()), 0);
  EXPECT_FALSE(rfs::cwd::archive_cwd().has_value());
  std::array<char, kPathBuf> buf{};
  ASSERT_NE(posix::getcwd(buf.data(), buf.size()), nullptr);
  EXPECT_EQ(fs::canonical(buf.data()), fs::canonical(dir_.path()));
}

TEST_F(PosixIo, RelativeOpenRoutesAfterMount) {
  errno = 0;
  EXPECT_EQ(posix::open("DIR/FILE.DAT", O_RDONLY, 0), -1);
  EXPECT_EQ(errno, ENOTSUP);
}

#endif  // !defined(_WIN32)
