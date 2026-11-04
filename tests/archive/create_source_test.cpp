#include <gtest/gtest.h>

#include <filesystem>

#include "../fs/test_support.hpp"
#include "rwzfs/archive/create.hpp"

namespace fs = std::filesystem;

TEST(CreateArchive, MissingSourceIsReported) {
  TempDir dir;
  const auto result = rwzfs::create_archive(dir.path() / "nope", dir.path() / "out.dwarfs");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), rwzfs::create_error::source_missing);
  EXPECT_FALSE(fs::exists(dir.path() / "out.dwarfs"));
}

#if !defined(_WIN32)  // creation is POSIX only for now
TEST(CreateArchive, MissingToolIsReportedAndLeavesNoOutput) {
  TempDir dir;
  rwzfs::create_options options;
  options.mkdwarfs = dir.path() / "no-such-mkdwarfs";
  const auto result = rwzfs::create_archive(dir.path(), dir.path() / "out.dwarfs", options);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), rwzfs::create_error::tool_not_found);
  EXPECT_FALSE(fs::exists(dir.path() / "out.dwarfs"));
  EXPECT_FALSE(fs::exists(dir.path() / "out.dwarfs.tmp"));
}

#endif

TEST(CreateArchive, DefaultCompressionMatchesCliSpec) {
  EXPECT_STREQ(rwzfs::kDefaultCompression, "zstd:level=12:wlog=27:strat=3");
}
