#if !defined(_WIN32)

#include <gtest/gtest.h>

#include <cerrno>
#include <chrono>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

#include "../fs/test_support.hpp"
#include "../support/fake_backend.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/filesystem.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/io/posix.hpp"

namespace fs = std::filesystem;
namespace rfs = rwzfs::fs;
namespace mounts = rwzfs::fs::mounts;
namespace posix = rwzfs::io::posix;

namespace {
constexpr const char* kFile = "/rwz/DIR/FILE.DAT";
constexpr const char* kDir = "/rwz/DIR";
constexpr std::int64_t kStamp = 1700000000;
}  // namespace

class OverlayMetadata : public ::testing::Test {
 protected:
  void SetUp() override {
    mounts::clear();
    rfs::cwd::clear();
    backend_ = std::make_shared<fake_backend>();
    backend_->add_file("DIR/FILE.DAT", "payload");
    ASSERT_TRUE(mounts::add("/rwz", backend_, overlay_.path()));
  }
  void TearDown() override {
    mounts::clear();
    rfs::cwd::clear();
  }

  fs::path data_copy() const { return overlay_.path() / "data" / "DIR" / "FILE.DAT"; }

  TempDir overlay_;
  std::shared_ptr<fake_backend> backend_;
};

TEST_F(OverlayMetadata, ChmodCopiesUpAndAppliesToOverlayCopy) {
  ASSERT_EQ(posix::chmod(kFile, 0600), 0);
  struct stat st{};
  ASSERT_EQ(posix::stat(kFile, &st), 0);
  EXPECT_EQ(st.st_mode & 07777, 0600u);
  EXPECT_TRUE(fs::exists(data_copy()));
  EXPECT_EQ(backend_->lookup("DIR/FILE.DAT")->mode & 07777, 0644u);  // archive untouched
}

TEST_F(OverlayMetadata, ChmodOnDirectoryIsNotPermitted) {
  errno = 0;
  EXPECT_EQ(posix::chmod(kDir, 0700), -1);
  EXPECT_EQ(errno, EPERM);
  EXPECT_FALSE(fs::exists(overlay_.path() / "data" / "DIR"));  // no tree copied
}

TEST_F(OverlayMetadata, UtimensatSetsModificationTimeOnFile) {
  const struct timespec times[2] = {{0, UTIME_OMIT}, {kStamp, 0}};
  ASSERT_EQ(posix::utimensat(AT_FDCWD, kFile, times, 0), 0);
  struct stat st{};
  ASSERT_EQ(posix::stat(kFile, &st), 0);
  EXPECT_EQ(st.st_mtime, static_cast<time_t>(kStamp));
  EXPECT_EQ(backend_->lookup("DIR/FILE.DAT")->mtime, 0);  // archive untouched
}

TEST_F(OverlayMetadata, UtimensatOnDirectoryIsNotPermitted) {
  const struct timespec times[2] = {{kStamp, 0}, {kStamp, 0}};
  errno = 0;
  EXPECT_EQ(posix::utimensat(AT_FDCWD, kDir, times, 0), -1);
  EXPECT_EQ(errno, EPERM);
}

TEST_F(OverlayMetadata, FsPermissionsCopyUp) {
  std::error_code ec;
  rfs::permissions(fs::path(kFile), fs::perms::owner_read | fs::perms::owner_write, ec);
  EXPECT_FALSE(ec);
  EXPECT_EQ(rfs::status(fs::path(kFile), ec).permissions() & fs::perms::group_all,
            fs::perms::none);
  EXPECT_TRUE(fs::exists(data_copy()));
}

TEST_F(OverlayMetadata, FsLastWriteTimeRoundTrip) {
  std::error_code ec;
  const auto sys = std::chrono::system_clock::time_point(std::chrono::seconds(kStamp));
  const auto t = std::chrono::clock_cast<fs::file_time_type::clock>(sys);
  rfs::last_write_time(fs::path(kFile), t, ec);
  ASSERT_FALSE(ec);
  const auto back = rfs::last_write_time(fs::path(kFile), ec);
  ASSERT_FALSE(ec);
  const auto back_sys = std::chrono::clock_cast<std::chrono::system_clock>(back);
  EXPECT_EQ(std::chrono::duration_cast<std::chrono::seconds>(back_sys.time_since_epoch()).count(),
            kStamp);
}

TEST_F(OverlayMetadata, ReadOnlyOpenNeverCopiesUp) {
  const int fd = posix::open(kFile, O_RDWR, 0);
  ASSERT_GE(fd, 0);
  char buf[3] = {};
  EXPECT_EQ(posix::read(fd, buf, 3), 3);
  EXPECT_EQ(std::string(buf, 3), "pay");
  posix::close(fd);
  EXPECT_FALSE(fs::exists(data_copy()));  // lazy: no write, no copy
}

TEST_F(OverlayMetadata, FirstWriteCopiesUpWholeFile) {
  const int fd = posix::open(kFile, O_RDWR, 0);
  ASSERT_GE(fd, 0);
  EXPECT_FALSE(fs::exists(data_copy()));
  EXPECT_EQ(posix::lseek(fd, 1, SEEK_SET), 1);
  EXPECT_EQ(posix::write(fd, "Z", 1), 1);
  posix::close(fd);
  EXPECT_TRUE(fs::exists(data_copy()));

  const int rd = posix::open(kFile, O_RDONLY, 0);
  ASSERT_GE(rd, 0);
  char buf[16] = {};
  EXPECT_EQ(posix::read(rd, buf, sizeof(buf)), 7);
  EXPECT_EQ(std::string(buf, 7), "pZyload");
  posix::close(rd);
}

TEST_F(OverlayMetadata, WriteOnlyDescriptorCannotRead) {
  const int fd = posix::open(kFile, O_WRONLY, 0);
  ASSERT_GE(fd, 0);
  char buf[1];
  errno = 0;
  EXPECT_EQ(posix::read(fd, buf, 1), -1);
  EXPECT_EQ(errno, EBADF);
  posix::close(fd);
  EXPECT_FALSE(fs::exists(data_copy()));  // still deferred
}

TEST_F(OverlayMetadata, TruncatingOpenCopiesUpImmediately) {
  const int fd = posix::open(kFile, O_WRONLY | O_TRUNC, 0);
  ASSERT_GE(fd, 0);
  EXPECT_TRUE(fs::exists(data_copy()));
  posix::close(fd);
  EXPECT_EQ(fs::file_size(data_copy()), 0u);
}

TEST_F(OverlayMetadata, ReadAfterSharedCopyUpFollowsOverlay) {
  const int deferred = posix::open(kFile, O_RDWR, 0);
  ASSERT_GE(deferred, 0);
  const int other = posix::open(kFile, O_RDWR, 0);
  ASSERT_GE(other, 0);
  EXPECT_EQ(posix::write(other, "Q", 1), 1);  // second descriptor copies the file up
  char buf[8] = {};
  EXPECT_EQ(posix::read(deferred, buf, sizeof(buf)), 7);
  EXPECT_EQ(std::string(buf, 7), "Qayload");  // deferred one now reads the overlay copy
  posix::close(deferred);
  posix::close(other);
}

#endif  // !defined(_WIN32)
