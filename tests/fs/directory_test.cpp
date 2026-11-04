#include "../support/platform_paths.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "../support/fake_backend.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/directory.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "test_support.hpp"

namespace fs = std::filesystem;
namespace rfs = rwzfs::fs;
namespace mounts = rwzfs::fs::mounts;

namespace {

std::set<std::string> names_of(const fs::path& dir) {
  std::error_code ec;
  std::set<std::string> names;
  for (const auto& e : rfs::directory_iterator(dir, ec)) {
    names.insert(e.path().filename().string());
  }
  return names;
}

}  // namespace

// Archive tree:
//   DIR/FILE.DAT ("payload"), DIR/SUB/inner.txt ("x"), DIR/EMPTYSUB/, EMPTY.TXT ("")
class ArchiveListing : public ::testing::Test {
 protected:
  void SetUp() override {
    mounts::clear();
    rfs::cwd::clear();
    auto backend = std::make_shared<fake_backend>();
    backend->add_file("DIR/FILE.DAT", "payload");
    backend->add_file("DIR/SUB/inner.txt", "x");
    backend->add_dir("DIR/EMPTYSUB");
    backend->add_file("EMPTY.TXT", "");
    ASSERT_TRUE(mounts::add(RWZ_ROOT "", backend));
  }
  void TearDown() override {
    mounts::clear();
    rfs::cwd::clear();
  }
};

TEST_F(ArchiveListing, ListsChildrenWithoutDotEntries) {
  std::error_code ec;
  std::set<std::string> names;
  for (const auto& e : rfs::directory_iterator(fs::path(RWZ_ROOT "/DIR"), ec)) {
    names.insert(e.path().filename().string());
  }
  EXPECT_FALSE(ec);
  EXPECT_EQ(names, (std::set<std::string>{"FILE.DAT", "SUB", "EMPTYSUB"}));
}

TEST_F(ArchiveListing, EntriesHaveAbsolutePathsAndMetadata) {
  std::error_code ec;
  rfs::directory_iterator it(fs::path(RWZ_ROOT "/DIR"), ec);
  ASSERT_FALSE(ec);
  for (; it != rfs::directory_iterator{}; ++it) {
    if (it->path().filename() == "FILE.DAT") {
      EXPECT_EQ(it->path(), fs::path(RWZ_ROOT "/DIR/FILE.DAT"));
      EXPECT_TRUE(it->is_regular_file(ec));
      EXPECT_FALSE(it->is_directory(ec));
      EXPECT_EQ(it->file_size(ec), 7u);
      EXPECT_EQ(it->status(ec).type(), fs::file_type::regular);
    }
    if (it->path().filename() == "SUB") {
      EXPECT_TRUE(it->is_directory(ec));
    }
  }
}

TEST_F(ArchiveListing, EmptyDirectoryEqualsEnd) {
  std::error_code ec;
  rfs::directory_iterator it(fs::path(RWZ_ROOT "/DIR/EMPTYSUB"), ec);
  EXPECT_FALSE(ec);
  EXPECT_TRUE(it == rfs::directory_iterator{});
}

TEST_F(ArchiveListing, PostfixIncrementAdvancesThroughAllEntries) {
  std::error_code ec;
  rfs::directory_iterator it(fs::path(RWZ_ROOT "/DIR"), ec);
  std::size_t count = 0;
  while (it != rfs::directory_iterator{}) {
    it++;
    ++count;
  }
  EXPECT_EQ(count, 3u);
}

TEST_F(ArchiveListing, MissingDirectoryReportsEnoent) {
  std::error_code ec;
  rfs::directory_iterator it(fs::path(RWZ_ROOT "/nope"), ec);
  EXPECT_EQ(ec, std::errc::no_such_file_or_directory);
  EXPECT_TRUE(it == rfs::directory_iterator{});
}

TEST_F(ArchiveListing, FileReportsEnotdir) {
  std::error_code ec;
  rfs::directory_iterator it(fs::path(RWZ_ROOT "/DIR/FILE.DAT"), ec);
  EXPECT_EQ(ec, std::errc::not_a_directory);
}

TEST_F(ArchiveListing, RelativeListingUsesArchiveCwd) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd(RWZ_ROOT "/DIR"));
  std::error_code ec;
  rfs::directory_iterator it(fs::path("."), ec);
  ASSERT_FALSE(ec);
  EXPECT_EQ(it->path().parent_path(), fs::path(RWZ_ROOT "/DIR"));
}

TEST_F(ArchiveListing, RecursiveVisitsTreeWithDepths) {
  std::error_code ec;
  std::set<std::pair<std::string, int>> seen;
  rfs::recursive_directory_iterator it(fs::path(RWZ_ROOT "/DIR"), ec);
  ASSERT_FALSE(ec);
  for (; it != rfs::recursive_directory_iterator{}; ++it) {
    seen.insert({it->path().filename().string(), it.depth()});
  }
  EXPECT_EQ(seen, (std::set<std::pair<std::string, int>>{
                      {"FILE.DAT", 0}, {"SUB", 0}, {"EMPTYSUB", 0}, {"inner.txt", 1}}));
}

TEST_F(ArchiveListing, DisableRecursionSkipsSubtree) {
  std::error_code ec;
  std::set<std::string> seen;
  rfs::recursive_directory_iterator it(fs::path(RWZ_ROOT "/DIR"), ec);
  for (; it != rfs::recursive_directory_iterator{}; ++it) {
    seen.insert(it->path().filename().string());
    if (it->path().filename() == "SUB") it.disable_recursion_pending();
  }
  EXPECT_EQ(seen.count("inner.txt"), 0u);
  EXPECT_EQ(seen.count("SUB"), 1u);
}

TEST_F(ArchiveListing, RangeForWorksWithAuto) {
  std::error_code ec;
  std::size_t count = 0;
  for (const auto& e : rfs::directory_iterator(fs::path(RWZ_ROOT ""), ec)) {
    (void)e;
    ++count;
  }
  EXPECT_EQ(count, 2u);  // DIR and EMPTY.TXT
}

TEST(DirectoryNoBackend, ListingWithoutBackendIsNotSupported) {
  mounts::clear();
  rfs::cwd::clear();
  ASSERT_TRUE(mounts::add(RWZ_ROOT ""));
  std::error_code ec;
  rfs::directory_iterator it(fs::path(RWZ_ROOT "/DIR"), ec);
  EXPECT_EQ(ec, std::errc::not_supported);
  mounts::clear();
}

// Host passthrough: results must match std::filesystem.

TEST(DirectoryHost, ListingMatchesStd) {
  mounts::clear();
  TempDir dir;
  fs::create_directories(dir.path() / "sub" / "deeper");
  std::ofstream(dir.path() / "a.txt") << "a";
  std::ofstream(dir.path() / "sub" / "b.txt") << "b";

  std::set<std::string> std_names;
  for (const auto& e : fs::directory_iterator(dir.path())) {
    std_names.insert(e.path().filename().string());
  }
  EXPECT_EQ(names_of(dir.path()), std_names);
}

TEST(DirectoryHost, RecursiveCountMatchesStd) {
  mounts::clear();
  TempDir dir;
  fs::create_directories(dir.path() / "sub" / "deeper");
  std::ofstream(dir.path() / "sub" / "b.txt") << "b";

  std::size_t std_count = 0;
  for (auto it = fs::recursive_directory_iterator(dir.path());
       it != fs::recursive_directory_iterator{}; ++it) {
    ++std_count;
  }
  std::error_code ec;
  std::size_t count = 0;
  for (rfs::recursive_directory_iterator it(dir.path(), ec);
       it != rfs::recursive_directory_iterator{}; ++it) {
    ++count;
  }
  EXPECT_FALSE(ec);
  EXPECT_EQ(count, std_count);
}
