#include "../support/platform_paths.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "rwzfs/fs/walk.hpp"
#include "test_support.hpp"

namespace fs = std::filesystem;
namespace rfs = rwzfs::fs;

TEST(Walk, RelativeIsUnchanged) {
  EXPECT_EQ(rfs::walk_lexically("a/../b"), fs::path("a/../b"));
}

TEST(Walk, HostAbsoluteKeepsDotDotForOs) {
  EXPECT_EQ(rfs::walk_lexically(HOST_ROOT "abs/x/../y"), fs::path(HOST_ROOT "abs/x/../y"));
}

#if !defined(_WIN32)

TEST(Walk, ArchiveDotDotIsAppliedLexically) {
  EXPECT_EQ(rfs::walk_lexically(RWZ_ROOT "/sub/./a/../b"), fs::path(RWZ_ROOT "/sub/b"));
}

TEST(Walk, ArchiveRootDotDotLeavesToHost) {
  EXPECT_EQ(rfs::walk_lexically(RWZ_ROOT "/.."), fs::path("/"));
}

TEST(Walk, EscapeFromArchiveKeepsRestRaw) {
  EXPECT_EQ(rfs::walk_lexically(RWZ_ROOT "/sub/../../host/x"), fs::path(HOST_ROOT "host/x"));
  EXPECT_EQ(rfs::walk_lexically(RWZ_ROOT "/../link/../x"), fs::path(HOST_ROOT "link/../x"));
}

TEST(Walk, HostSymlinkDotDotIsFollowed) {
  TempDir dir;
  fs::create_directories(dir.path() / "a" / "b");
  std::ofstream(dir.path() / "a" / "c.txt") << "x";
  fs::create_directory_symlink(dir.path() / "a" / "b", dir.path() / "link");

  const auto p = dir.path() / "link" / ".." / "c.txt";
  const auto walked = rfs::walk_lexically(p);
  EXPECT_EQ(walked, p);  // not lexically reduced
  EXPECT_TRUE(fs::exists(walked));  // OS resolves through the symlink
  EXPECT_FALSE(fs::exists(p.lexically_normal()));  // lexical reduction would be wrong
}

#endif  // !defined(_WIN32)
