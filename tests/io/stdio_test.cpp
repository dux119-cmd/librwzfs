#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/io/stdio.hpp"
#include "../fs/test_support.hpp"

namespace fs = std::filesystem;
namespace stdio = rwzfs::io::stdio;
namespace mounts = rwzfs::fs::mounts;
namespace rfs = rwzfs::fs;

namespace {
constexpr const char* kArchiveFile = "/rwz/a.txt";
constexpr std::size_t kBuf = 16;
}  // namespace

class StdioIo : public ::testing::Test {
 protected:
  void SetUp() override {
    mounts::clear();
    rfs::cwd::clear();
    ASSERT_TRUE(mounts::add("/rwz"));
    file_ = dir_.path() / "file.txt";
    std::ofstream(file_) << "hello";
  }
  void TearDown() override {
    mounts::clear();
    rfs::cwd::clear();
  }

  TempDir dir_;
  fs::path file_;
};

TEST_F(StdioIo, FopenFreadFtellRoundTrip) {
  std::FILE* fp = stdio::fopen(file_.string().c_str(), "rb");
  ASSERT_NE(fp, nullptr);
  char buf[kBuf] = {};
  EXPECT_EQ(stdio::fread(buf, 1, 5, fp), 5u);
  EXPECT_EQ(std::string(buf, 5), "hello");
  EXPECT_EQ(stdio::ftell(fp), 5L);
  EXPECT_EQ(stdio::fflush(fp), 0);
  std::fclose(fp);
}

TEST_F(StdioIo, FopenArchivePathFailsWithEnotsup) {
  errno = 0;
  EXPECT_EQ(stdio::fopen(kArchiveFile, "rb"), nullptr);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(StdioIo, FopenRelativeUnderArchiveCwdFailsWithEnotsup) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd("/rwz/sub"));
  errno = 0;
  EXPECT_EQ(stdio::fopen("x.txt", "rb"), nullptr);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(StdioIo, RemoveHostFile) {
  EXPECT_EQ(stdio::remove(file_.string().c_str()), 0);
  EXPECT_FALSE(fs::exists(file_));
}

TEST_F(StdioIo, RemoveArchivePathFailsWithEnotsup) {
  errno = 0;
  EXPECT_EQ(stdio::remove(kArchiveFile), -1);
  EXPECT_EQ(errno, ENOTSUP);
}

TEST_F(StdioIo, RenameHostFile) {
  const auto dst = dir_.path() / "renamed.txt";
  EXPECT_EQ(stdio::rename(file_.string().c_str(), dst.string().c_str()), 0);
  EXPECT_TRUE(fs::exists(dst));
}

TEST_F(StdioIo, RenameIntoArchiveFailsWithEnotsup) {
  errno = 0;
  EXPECT_EQ(stdio::rename(file_.string().c_str(), kArchiveFile), -1);
  EXPECT_EQ(errno, ENOTSUP);
  EXPECT_TRUE(fs::exists(file_));
}

TEST_F(StdioIo, InputStreamReadsHostFile) {
  auto in = stdio::input_file_stream(file_.string());
  ASSERT_TRUE(in.is_open());
  std::string text;
  in >> text;
  EXPECT_EQ(text, "hello");
}

TEST_F(StdioIo, InputStreamForArchivePathIsFailed) {
  auto in = stdio::input_file_stream(kArchiveFile);
  EXPECT_FALSE(in.is_open());
  EXPECT_TRUE(in.fail());
}

TEST_F(StdioIo, OutputStreamWritesHostFile) {
  const auto out_path = dir_.path() / "out.txt";
  {
    auto out = stdio::output_file_stream(out_path.string());
    ASSERT_TRUE(out.is_open());
    out << "data";
  }
  EXPECT_EQ(fs::file_size(out_path), 4u);
}

TEST_F(StdioIo, OutputStreamForArchivePathIsFailed) {
  auto out = stdio::output_file_stream(kArchiveFile);
  EXPECT_FALSE(out.is_open());
  EXPECT_TRUE(out.fail());
}
