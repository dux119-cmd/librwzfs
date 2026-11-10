#if !defined(_WIN32)

#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "../fs/test_support.hpp"
#include "../support/fake_backend.hpp"
#include "../support/platform_paths.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/io/stdio.hpp"

namespace fs = std::filesystem;
namespace mounts = rwzfs::fs::mounts;
namespace rfs = rwzfs::fs;
namespace stdio = rwzfs::io::stdio;

namespace {

#if defined(__GLIBC__) || defined(__APPLE__) || defined(__FreeBSD__)
constexpr bool kLazyPlatform = true;
#else
constexpr bool kLazyPlatform = false;
#endif

std::string read_host(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string read_archive_view(const char* path) {
  std::FILE* fp = stdio::fopen(path, "rb");
  if (fp == nullptr) return "<missing>";
  char buf[32] = {};
  const auto n = std::fread(buf, 1, sizeof(buf), fp);
  stdio::fclose(fp);
  return std::string(buf, n);
}

}  // namespace

class StdioLazy : public ::testing::Test {
 protected:
  void SetUp() override {
    mounts::clear();
    rfs::cwd::clear();
    backend_ = std::make_shared<fake_backend>();
    backend_->add_file("DIR/FILE.DAT", "payload");
    ASSERT_TRUE(mounts::add(RWZ_ROOT, backend_, overlay_.path()));
  }
  void TearDown() override {
    mounts::clear();
    rfs::cwd::clear();
  }

  fs::path data_copy() const { return overlay_.path() / "data" / "DIR" / "FILE.DAT"; }

  TempDir overlay_;
  std::shared_ptr<fake_backend> backend_;
};

TEST_F(StdioLazy, ReadWriteOpenDoesNotCopyUpUntilWrite) {
  if (!kLazyPlatform) GTEST_SKIP() << "no callback stream on this platform: copy happens at open";
  std::FILE* fp = stdio::fopen(RWZ_ROOT "/DIR/FILE.DAT", "r+b");
  ASSERT_NE(fp, nullptr);
  EXPECT_FALSE(fs::exists(data_copy()));  // opened, not written: no copy
  char buf[3] = {};
  EXPECT_EQ(std::fread(buf, 1, 3, fp), 3u);
  EXPECT_EQ(std::string(buf, 3), "pay");
  EXPECT_EQ(stdio::fclose(fp), 0);
  EXPECT_FALSE(fs::exists(data_copy()));  // reading never copies
}

TEST_F(StdioLazy, FirstWriteCopiesUpAndKeepsFileContent) {
  std::FILE* fp = stdio::fopen(RWZ_ROOT "/DIR/FILE.DAT", "r+b");
  ASSERT_NE(fp, nullptr);
  ASSERT_EQ(std::fseek(fp, 1, SEEK_SET), 0);
  EXPECT_EQ(std::fwrite("Z", 1, 1, fp), 1u);
  EXPECT_EQ(std::fflush(fp), 0);
  if (kLazyPlatform) {
    EXPECT_TRUE(fs::exists(data_copy()));
  }
  EXPECT_EQ(stdio::fclose(fp), 0);
  EXPECT_EQ(read_archive_view(RWZ_ROOT "/DIR/FILE.DAT"), "pZyload");
  EXPECT_EQ(read_host(data_copy()), "pZyload");
}

TEST_F(StdioLazy, AppendModeWritesAtEnd) {
  std::FILE* fp = stdio::fopen(RWZ_ROOT "/DIR/FILE.DAT", "a");
  ASSERT_NE(fp, nullptr);
  EXPECT_EQ(std::fwrite("!", 1, 1, fp), 1u);
  EXPECT_EQ(stdio::fclose(fp), 0);
  EXPECT_EQ(read_archive_view(RWZ_ROOT "/DIR/FILE.DAT"), "payload!");
}

TEST_F(StdioLazy, FseekThenReadOnLazyStream) {
  std::FILE* fp = stdio::fopen(RWZ_ROOT "/DIR/FILE.DAT", "r+b");
  ASSERT_NE(fp, nullptr);
  EXPECT_EQ(stdio::fseek(fp, 4, SEEK_SET), 0);
  char buf[3] = {};
  EXPECT_EQ(std::fread(buf, 1, 3, fp), 3u);
  EXPECT_EQ(std::string(buf, 3), "oad");
  stdio::fclose(fp);
}

TEST_F(StdioLazy, ReadPlusOfMissingFileFails) {
  errno = 0;
  EXPECT_EQ(stdio::fopen(RWZ_ROOT "/NOPE.TXT", "r+b"), nullptr);
  EXPECT_EQ(errno, ENOENT);
}

TEST_F(StdioLazy, WriteModeCreatesNewOverlayFile) {
  std::FILE* fp = stdio::fopen(RWZ_ROOT "/DIR/NEW.TXT", "w");
  ASSERT_NE(fp, nullptr);
  EXPECT_EQ(std::fwrite("new", 1, 3, fp), 3u);
  EXPECT_EQ(stdio::fclose(fp), 0);
  EXPECT_EQ(read_archive_view(RWZ_ROOT "/DIR/NEW.TXT"), "new");
}

TEST_F(StdioLazy, FseekoAndFtelloOnHostStream) {
  const auto out = overlay_.path() / "host.bin";
  std::FILE* fp = stdio::fopen(out.string().c_str(), "w+b");
  ASSERT_NE(fp, nullptr);
  EXPECT_EQ(std::fwrite("abcdef", 1, 6, fp), 6u);
  EXPECT_EQ(stdio::fseeko(fp, 2, SEEK_SET), 0);
  EXPECT_EQ(stdio::ftello(fp), static_cast<off_t>(2));
  stdio::fclose(fp);
}

#endif  // !defined(_WIN32)
