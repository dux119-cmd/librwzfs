#if !defined(_WIN32)

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <memory>
#include <string>

#include "../support/fake_backend.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/io/stdio.hpp"

namespace stdio = rwzfs::io::stdio;
namespace mounts = rwzfs::fs::mounts;
namespace rfs = rwzfs::fs;

namespace {
constexpr const char* kFile = "/rwz/DIR/FILE.DAT";
constexpr const char* kEmpty = "/rwz/EMPTY.TXT";
constexpr std::size_t kPayloadSize = 7;
}  // namespace

class StdioArchive : public ::testing::Test {
 protected:
  void SetUp() override {
    mounts::clear();
    rfs::cwd::clear();
    auto backend = std::make_shared<fake_backend>();
    backend->add_file("DIR/FILE.DAT", "payload");
    backend->add_file("EMPTY.TXT", "");
    ASSERT_TRUE(mounts::add("/rwz", backend));
  }
  void TearDown() override {
    mounts::clear();
    rfs::cwd::clear();
  }
};

TEST_F(StdioArchive, ReadsPayloadWithFreadFtellAndFseek) {
  std::FILE* fp = stdio::fopen(kFile, "rb");
  ASSERT_NE(fp, nullptr);
  char buf[16] = {};
  EXPECT_EQ(stdio::fread(buf, 1, sizeof(buf), fp), kPayloadSize);
  EXPECT_EQ(std::string(buf, kPayloadSize), "payload");
  EXPECT_EQ(stdio::ftell(fp), static_cast<long>(kPayloadSize));
  EXPECT_EQ(std::fseek(fp, 0, SEEK_SET), 0);
  EXPECT_EQ(stdio::fread(buf, 1, 3, fp), 3u);
  EXPECT_EQ(std::string(buf, 3), "pay");
  EXPECT_EQ(stdio::fclose(fp), 0);
}

TEST_F(StdioArchive, EmptyFileReadsNothing) {
  std::FILE* fp = stdio::fopen(kEmpty, "rb");
  ASSERT_NE(fp, nullptr);
  char buf[4] = {};
  EXPECT_EQ(stdio::fread(buf, 1, sizeof(buf), fp), 0u);
  EXPECT_EQ(stdio::fclose(fp), 0);
}

TEST_F(StdioArchive, ArchiveStreamIsReadOnly) {
  std::FILE* fp = stdio::fopen(kFile, "rb");
  ASSERT_NE(fp, nullptr);
  EXPECT_EQ(std::fwrite("x", 1, 1, fp), 0u);
  EXPECT_EQ(stdio::fclose(fp), 0);
}

TEST_F(StdioArchive, WriteModesFailWithErofs) {
  for (const char* mode : {"w", "wb", "a", "r+", "w+"}) {
    errno = 0;
    EXPECT_EQ(stdio::fopen(kFile, mode), nullptr) << mode;
    EXPECT_EQ(errno, EROFS) << mode;
  }
}

TEST_F(StdioArchive, MissingFileFailsWithEnoent) {
  errno = 0;
  EXPECT_EQ(stdio::fopen("/rwz/nope", "rb"), nullptr);
  EXPECT_EQ(errno, ENOENT);
}

TEST_F(StdioArchive, DirectoryFailsWithEisdir) {
  errno = 0;
  EXPECT_EQ(stdio::fopen("/rwz/DIR", "rb"), nullptr);
  EXPECT_EQ(errno, EISDIR);
}

TEST_F(StdioArchive, RelativeFopenUnderArchiveCwd) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd("/rwz/DIR"));
  std::FILE* fp = stdio::fopen("FILE.DAT", "rb");
  ASSERT_NE(fp, nullptr);
  char buf[8] = {};
  EXPECT_EQ(stdio::fread(buf, 1, sizeof(buf), fp), kPayloadSize);
  stdio::fclose(fp);
}

TEST_F(StdioArchive, RemoveAndRenameOnArchiveFailWithErofs) {
  errno = 0;
  EXPECT_EQ(stdio::remove(kFile), -1);
  EXPECT_EQ(errno, EROFS);
  errno = 0;
  EXPECT_EQ(stdio::rename(kFile, "/rwz/other"), -1);
  EXPECT_EQ(errno, EROFS);
}

#endif  // !defined(_WIN32)
