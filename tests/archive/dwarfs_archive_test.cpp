#include <gtest/gtest.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "fixture.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/filesystem.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/fs/resolve.hpp"
#include "rwzfs/io/posix.hpp"
#include "rwzfs/mount.hpp"
#include "../fs/test_support.hpp"

namespace fs = std::filesystem;
namespace mounts = rwzfs::fs::mounts;
namespace rfs = rwzfs::fs;
namespace posix = rwzfs::io::posix;

namespace {

constexpr std::size_t kPayloadSize = 7;

std::set<std::string> list_names(const char* dir_path) {
  std::set<std::string> names;
  DIR* dir = posix::opendir(dir_path);
  if (dir == nullptr) return names;
  while (const struct dirent* e = posix::readdir(dir)) {
    names.insert(e->d_name);
  }
  posix::closedir(dir);
  return names;
}

}  // namespace

// The fixture archive is rebuilt by CTest before these tests run.
class DwarfsArchive : public ::testing::Test {
 protected:
  void SetUp() override {
    mounts::clear();
    rfs::cwd::clear();
    rwzfs::mount_options options;
    options.write_root = writes_.path();  // keep overlays out of the real user state directory
    ASSERT_EQ(rwzfs::mount_archive(kFixtureArchive, "/rwz", options), rwzfs::MountError::None);
  }
  void TearDown() override {
    mounts::clear();
    rfs::cwd::clear();
  }

  TempDir writes_;
};

TEST_F(DwarfsArchive, RootContainsExactlyTheFixtureEntries) {
  EXPECT_EQ(list_names("/rwz"),
            (std::set<std::string>{".", "..", "DIR", "LINK_REL", "LINK_ABS", "HOSTLINK",
                                   "DIRLINK", "NESTLINK", "DANGLING", "LOOP_A", "LOOP_B"}));
}

TEST_F(DwarfsArchive, DirectoryContainsExactlyTheFixtureEntries) {
  EXPECT_EQ(list_names("/rwz/DIR"),
            (std::set<std::string>{".", "..", "FILE.DAT", "SUB", "UP", "UPROOT"}));
}

TEST_F(DwarfsArchive, LinkTargetsAreReadVerbatim) {
  std::error_code ec;
  EXPECT_EQ(rfs::read_symlink(fs::path("/rwz/LINK_REL"), ec), fs::path("DIR/FILE.DAT"));
  EXPECT_EQ(rfs::read_symlink(fs::path("/rwz/LINK_ABS"), ec), fs::path("/rwz/DIR/FILE.DAT"));
  EXPECT_EQ(rfs::read_symlink(fs::path("/rwz/DIR/UP"), ec), fs::path("../DIR/FILE.DAT"));
  EXPECT_FALSE(ec);
}

TEST_F(DwarfsArchive, RelativeLinkReadsPayload) {
  const int fd = posix::open("/rwz/LINK_REL", O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  char buf[16] = {};
  EXPECT_EQ(posix::read(fd, buf, sizeof(buf)), static_cast<ssize_t>(kPayloadSize));
  EXPECT_EQ(std::string(buf, kPayloadSize), kFixturePayload);
  posix::close(fd);
}

TEST_F(DwarfsArchive, AbsoluteLinkReadsPayload) {
  std::error_code ec;
  EXPECT_TRUE(rfs::is_regular_file(fs::path("/rwz/LINK_ABS"), ec));
  EXPECT_FALSE(ec);
  EXPECT_TRUE(rfs::is_symlink(fs::path("/rwz/LINK_ABS"), ec));
}

TEST_F(DwarfsArchive, UpLinkResolvesWithinDirectory) {
  std::error_code ec;
  EXPECT_EQ(rfs::resolve(fs::path("/rwz/DIR/UP"), ec), fs::path("/rwz/DIR/FILE.DAT"));
  EXPECT_FALSE(ec);
}

TEST_F(DwarfsArchive, DirectoryLinkResolvesChildren) {
  std::error_code ec;
  EXPECT_EQ(rfs::resolve(fs::path("/rwz/DIRLINK/FILE.DAT"), ec), fs::path("/rwz/DIR/FILE.DAT"));
  EXPECT_TRUE(rfs::is_directory(fs::path("/rwz/DIRLINK"), ec));
}

TEST_F(DwarfsArchive, DotDotAfterDirectoryLinkFollowsTheLinkTarget) {
  std::error_code ec;
  EXPECT_EQ(rfs::resolve(fs::path("/rwz/NESTLINK/../FILE.DAT"), ec),
            fs::path("/rwz/DIR/FILE.DAT"));
}

TEST_F(DwarfsArchive, AbsoluteHostLinkLeavesTheArchive) {
  std::error_code ec;
  EXPECT_EQ(rfs::resolve(fs::path("/rwz/HOSTLINK"), ec), fs::path("/etc/rwz-test-host"));
  EXPECT_FALSE(mounts::is_archive_path(fs::path("/etc/rwz-test-host")));
}

TEST_F(DwarfsArchive, RelativeLinkClimbingAboveRootLeavesTheArchive) {
  std::error_code ec;
  EXPECT_EQ(rfs::resolve(fs::path("/rwz/DIR/UPROOT"), ec), fs::path("/hostfile"));
}

TEST_F(DwarfsArchive, DanglingLinkIsMissing) {
  std::error_code ec;
  EXPECT_FALSE(rfs::exists(fs::path("/rwz/DANGLING"), ec));
  EXPECT_FALSE(ec);
  EXPECT_TRUE(rfs::is_symlink(fs::path("/rwz/DANGLING"), ec));
}

TEST_F(DwarfsArchive, LoopFailsWithEloop) {
  errno = 0;
  EXPECT_EQ(posix::open("/rwz/LOOP_A", O_RDONLY, 0), -1);
  EXPECT_EQ(errno, ELOOP);
}

TEST_F(DwarfsArchive, ReadsExactPayload) {
  const int fd = posix::open("/rwz/DIR/FILE.DAT", O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  char buf[16] = {};
  EXPECT_EQ(posix::read(fd, buf, sizeof(buf)), static_cast<ssize_t>(kPayloadSize));
  EXPECT_EQ(std::string(buf, kPayloadSize), kFixturePayload);
  EXPECT_EQ(posix::close(fd), 0);
}

TEST_F(DwarfsArchive, StatReportsRegularFileAndSize) {
  struct stat st{};
  ASSERT_EQ(posix::stat("/rwz/DIR/FILE.DAT", &st), 0);
  EXPECT_TRUE(S_ISREG(st.st_mode));
  EXPECT_EQ(st.st_size, static_cast<off_t>(kPayloadSize));
}

TEST_F(DwarfsArchive, ExistsAndTypePredicates) {
  std::error_code ec;
  EXPECT_TRUE(rfs::exists("/rwz/DIR/FILE.DAT", ec));
  EXPECT_TRUE(rfs::is_regular_file("/rwz/DIR/FILE.DAT", ec));
  EXPECT_TRUE(rfs::is_directory("/rwz/DIR", ec));
  EXPECT_FALSE(rfs::exists("/rwz/nope", ec));
  EXPECT_FALSE(ec);
}

TEST_F(DwarfsArchive, RelativePathsResolveAfterMount) {
  std::error_code ec;
  EXPECT_TRUE(rfs::exists("DIR/FILE.DAT", ec));
  rfs::current_path(fs::path("/rwz/DIR"), ec);
  ASSERT_FALSE(ec);
  const int fd = posix::open("FILE.DAT", O_RDONLY, 0);
  ASSERT_GE(fd, 0);
  posix::close(fd);
}

TEST_F(DwarfsArchive, MissingFileFailsWithEnoent) {
  errno = 0;
  EXPECT_EQ(posix::open("/rwz/nope", O_RDONLY, 0), -1);
  EXPECT_EQ(errno, ENOENT);
}

TEST_F(DwarfsArchive, WritesGoToOverlayAndLeaveArchiveUnchanged) {
  const int fd = posix::open("/rwz/DIR/FILE.DAT", O_WRONLY, 0);
  ASSERT_GE(fd, 0);
  EXPECT_EQ(posix::write(fd, "P", 1), 1);
  posix::close(fd);

  const int rd = posix::open("/rwz/DIR/FILE.DAT", O_RDONLY, 0);
  ASSERT_GE(rd, 0);
  char buf[16] = {};
  EXPECT_EQ(posix::read(rd, buf, sizeof(buf)), static_cast<ssize_t>(kPayloadSize));
  EXPECT_EQ(std::string(buf, kPayloadSize), "Payload");
  posix::close(rd);
  EXPECT_TRUE(fs::exists(writes_.path() / "fixture-writes" / "data" / "DIR" / "FILE.DAT"));
}
