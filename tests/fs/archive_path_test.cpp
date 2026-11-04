#include "../support/platform_paths.hpp"
#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "rwzfs/fs/archive_path.hpp"
#include <optional>

namespace fs = std::filesystem;
namespace rfs = rwzfs::fs;

struct NormalizeCase {
  std::string_view input;
  std::string_view expected;
};

// Rows mirror the table in docs/archive_paths.md.
class NormalizeArchivePath : public ::testing::TestWithParam<NormalizeCase> {};

TEST_P(NormalizeArchivePath, MatchesSpec) {
  const auto& c = GetParam();
  const auto result = rfs::normalize_archive_path(c.input);
  ASSERT_TRUE(result.has_value()) << "input: " << c.input;
  EXPECT_EQ(*result, c.expected);
}

INSTANTIATE_TEST_SUITE_P(
    SpecTable, NormalizeArchivePath,
    ::testing::Values(
        NormalizeCase{"a/b/../c", "a/c"},
        NormalizeCase{HOST_ROOT "a/./b//c/", "a/b/c"},
        NormalizeCase{"a/b/..", "a"},
        NormalizeCase{".", ""},
        NormalizeCase{HOST_ROOT "../x", "x"},
        NormalizeCase{HOST_ROOT "a/../../../x", "x"},
        NormalizeCase{"a/..", ""},
        NormalizeCase{"/", ""},
        NormalizeCase{"", ""},
        NormalizeCase{"a/b", "a/b"},
        NormalizeCase{HOST_ROOT "a/b", "a/b"},
        NormalizeCase{" a b/.hidden/\xc3\xbc.txt", " a b/.hidden/\xc3\xbc.txt"},
        NormalizeCase{"a/...b", "a/...b"}));

class NormalizeArchivePathError : public ::testing::TestWithParam<std::string_view> {};

TEST_P(NormalizeArchivePathError, RejectsRelativeEscape) {
  const auto result = rfs::normalize_archive_path(GetParam());
  ASSERT_FALSE(result.has_value()) << "input: " << GetParam();
  EXPECT_EQ(result.error(), rfs::archive_path_error::escapes_root);
}

INSTANTIATE_TEST_SUITE_P(SpecTable, NormalizeArchivePathError,
                         ::testing::Values(std::string_view{"a/../../x"},
                                           std::string_view{"../x"},
                                           std::string_view{"./../x"}));

#if defined(_WIN32)
TEST(NormalizeArchivePathWindows, BackslashIsSeparator) {
  const auto result = rfs::normalize_archive_path("a\\b\\..\\c");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "a/c");
}
#endif

#if !defined(_WIN32)

TEST(ArchiveRoot, IsMagicPrefix) {
  EXPECT_EQ(rfs::archive_root(), fs::path(RWZ_ROOT ""));
}

TEST(ArchiveAbsolute, ExpandsRelativeTextUnderRoot) {
  EXPECT_EQ(*rfs::to_archive_absolute("DIR/FILE"), fs::path(RWZ_ROOT "/DIR/FILE"));
  EXPECT_EQ(*rfs::to_archive_absolute(HOST_ROOT "a/../b"), fs::path(RWZ_ROOT "/b"));
}

TEST(ArchiveAbsolute, EmptyTextIsRoot) {
  EXPECT_EQ(*rfs::to_archive_absolute(""), fs::path(RWZ_ROOT ""));
}

TEST(ArchiveAbsolute, RelativeEscapeIsRejected) {
  const auto result = rfs::to_archive_absolute("../x");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), rfs::archive_path_error::escapes_root);
}

TEST(ArchiveRelative, StripsRoot) {
  EXPECT_EQ(rfs::to_archive_relative(RWZ_ROOT "/DIR/FILE"), std::optional<std::string>("DIR/FILE"));
}

TEST(ArchiveRelative, RootIsEmptyText) {
  EXPECT_EQ(rfs::to_archive_relative(RWZ_ROOT ""), std::optional<std::string>(""));
  EXPECT_EQ(rfs::to_archive_relative(RWZ_ROOT "/"), std::optional<std::string>(""));
}

TEST(ArchiveRelative, OutsideRootIsNullopt) {
  EXPECT_FALSE(rfs::to_archive_relative(HOST_ROOT "rwzx/y").has_value());
  EXPECT_FALSE(rfs::to_archive_relative(HOST_ROOT "other").has_value());
  EXPECT_FALSE(rfs::to_archive_relative(RWZ_ROOT "/a/../../x").has_value());
}

TEST(ArchiveRoundTrip, RelativeToAbsoluteAndBack) {
  const auto absolute = rfs::to_archive_absolute("dir/sub/file.txt");
  ASSERT_TRUE(absolute.has_value());
  EXPECT_EQ(rfs::to_archive_relative(*absolute), std::optional<std::string>("dir/sub/file.txt"));
}

#endif  // !defined(_WIN32)
