#if !defined(_WIN32)

#include <gtest/gtest.h>

#include <cerrno>
#include <cstddef>
#include <array>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <string>
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
    ASSERT_TRUE(mounts::add("/rwzfs-test-mount"));
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

TEST_F(PosixIo, OpenArchivePathFailsWithEnotsup) {
  errno = 0;
  EXPECT_EQ(posix::open("/rwzfs-test-mount/a.txt", O_RDONLY, 0), -1);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(PosixIo, OpenRelativeUnderArchiveCwdFailsWithEnotsup) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd("/rwzfs-test-mount/sub"));
  errno = 0;
  EXPECT_EQ(posix::open("x.txt", O_RDONLY, 0), -1);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(PosixIo, GetcwdPassesThroughWithoutArchiveCwd) {
  std::array<char, 4096> buf{};
  ASSERT_NE(posix::getcwd(buf.data(), buf.size()), nullptr);
  EXPECT_EQ(fs::path(buf.data()), fs::current_path());
}

TEST_F(PosixIo, GetcwdReturnsArchiveCwd) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd("/rwzfs-test-mount/sub"));
  std::array<char, 64> buf{};
  ASSERT_NE(posix::getcwd(buf.data(), buf.size()), nullptr);
  EXPECT_STREQ(buf.data(), "/rwzfs-test-mount/sub");
}

TEST_F(PosixIo, GetcwdTooSmallFailsWithErange) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd("/rwzfs-test-mount/sub"));
  std::array<char, kTinyBuf> buf{};
  errno = 0;
  EXPECT_EQ(posix::getcwd(buf.data(), buf.size()), nullptr);
  EXPECT_EQ(errno, ERANGE);
}

TEST_F(PosixIo, ChdirIntoArchiveFailsWithEnotsup) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd("/rwzfs-test-mount/sub"));
  errno = 0;
  EXPECT_EQ(posix::chdir("/rwzfs-test-mount/other"), -1);
  EXPECT_EQ(errno, ENOTSUP);
  EXPECT_EQ(rfs::cwd::archive_cwd(), fs::path("/rwzfs-test-mount/sub"));
}

TEST_F(PosixIo, ChdirToHostSucceedsAndClearsArchiveCwd) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd("/rwzfs-test-mount/sub"));
  EXPECT_EQ(posix::chdir(dir_.path().c_str()), 0);
  EXPECT_FALSE(rfs::cwd::archive_cwd().has_value());
  std::array<char, 4096> buf{};
  ASSERT_NE(posix::getcwd(buf.data(), buf.size()), nullptr);
  EXPECT_EQ(fs::canonical(buf.data()), fs::canonical(dir_.path()));
}

#endif  // !defined(_WIN32)
