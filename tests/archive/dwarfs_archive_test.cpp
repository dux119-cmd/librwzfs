#include <gtest/gtest.h>

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
#include "rwzfs/io/posix.hpp"
#include "rwzfs/mount.hpp"

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
    ASSERT_EQ(rwzfs::mount_archive(kFixtureArchive), rwzfs::MountError::None);
  }
  void TearDown() override {
    mounts::clear();
    rfs::cwd::clear();
  }
};

TEST_F(DwarfsArchive, RootContainsExactlyTheFixtureDirectory) {
  EXPECT_EQ(list_names("/rwz"), (std::set<std::string>{".", "..", "DIR"}));
}

TEST_F(DwarfsArchive, DirectoryContainsExactlyTheFixtureFile) {
  EXPECT_EQ(list_names("/rwz/DIR"), (std::set<std::string>{".", "..", "FILE.DAT"}));
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

TEST_F(DwarfsArchive, WritesFailWithErofs) {
  errno = 0;
  EXPECT_EQ(posix::open("/rwz/DIR/FILE.DAT", O_WRONLY, 0), -1);
  EXPECT_EQ(errno, EROFS);
}
