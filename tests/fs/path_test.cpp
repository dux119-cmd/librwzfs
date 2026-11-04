#include "../support/platform_paths.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "rwzfs/fs/path.hpp"

namespace fs = std::filesystem;
namespace rfs = rwzfs::fs;

// PATH-01 .. PATH-03

TEST(PathC_Str, MatchesStandardPath) {
  const rfs::path p(HOST_ROOT "a/b");
  EXPECT_EQ(fs::path(rfs::c_str(p)), p);
}

TEST(PathString, MatchesStandardPath) {
  EXPECT_EQ(rfs::string(rfs::path(HOST_ROOT "a/b")), HOST_ROOT "a/b");
}

TEST(PathString, EmptyPathIsEmpty) {
  EXPECT_EQ(rfs::string(rfs::path{}), "");
}

TEST(PathAppend, AddsComponentWithoutMutatingInput) {
  const rfs::path base(HOST_ROOT "a/b");
  const auto result = rfs::append(base, "c");
  EXPECT_EQ(result, fs::path(HOST_ROOT "a/b/c"));
  EXPECT_EQ(base, fs::path(HOST_ROOT "a/b"));
}

TEST(PathAppend, MatchesStandardOperatorSlash) {
  const rfs::path base(HOST_ROOT "a/b");
  EXPECT_EQ(rfs::append(base, "x/y"), base / "x/y");
  EXPECT_EQ(rfs::append(base, HOST_ROOT "abs"), base / HOST_ROOT "abs");
}

// PATH-04: construction

TEST(PathMake, ConstructsFromText) {
  EXPECT_EQ(rfs::make_path(HOST_ROOT "a/b.txt"), fs::path(HOST_ROOT "a/b.txt"));
}

// PATH-05: operator/

TEST(PathJoin, MatchesOperatorSlash) {
  const rfs::path lhs(HOST_ROOT "a");
  const rfs::path rhs("b/c");
  EXPECT_EQ(rfs::join(lhs, rhs), lhs / rhs);
}

// PATH-06 .. PATH-14: component queries

TEST(PathFilename, ReturnsLastComponent) {
  EXPECT_EQ(rfs::filename(rfs::path(HOST_ROOT "a/b.txt")), "b.txt");
}

TEST(PathParentPath, ReturnsDirectory) {
  EXPECT_EQ(rfs::parent_path(rfs::path(HOST_ROOT "a/b.txt")), HOST_ROOT "a");
}

TEST(PathExtension, ReturnsSuffix) {
  EXPECT_EQ(rfs::extension(rfs::path(HOST_ROOT "a/b.tar.gz")), ".gz");
}

TEST(PathExtension, EmptyWhenNone) {
  EXPECT_EQ(rfs::extension(rfs::path(HOST_ROOT "a/README")), "");
}

TEST(PathIsAbsolute, DistinguishesAbsoluteFromRelative) {
  EXPECT_TRUE(rfs::is_absolute(rfs::path(HOST_ROOT "a")));
  EXPECT_FALSE(rfs::is_absolute(rfs::path("a")));
}

TEST(PathStem, ReturnsNameWithoutExtension) {
  EXPECT_EQ(rfs::stem(rfs::path(HOST_ROOT "a/b.tar.gz")), "b.tar");
}

TEST(PathLexicallyRelative, ComputesRelativePath) {
  EXPECT_EQ(rfs::lexically_relative(rfs::path(HOST_ROOT "a/b/c"), rfs::path(HOST_ROOT "a")),
            fs::path("b/c"));
}

TEST(PathReplaceExtension, ReplacesSuffixWithoutMutatingInput) {
  const rfs::path original(HOST_ROOT "a/b.txt");
  EXPECT_EQ(rfs::replace_extension(original, ".md"), fs::path(HOST_ROOT "a/b.md"));
  EXPECT_EQ(original, fs::path(HOST_ROOT "a/b.txt"));
}

TEST(PathRelativePath, StripsRoot) {
  EXPECT_EQ(rfs::relative_path(rfs::path(HOST_ROOT "a/b")), fs::path("a/b"));
}

TEST(PathU8String, MatchesStandardForAscii) {
  const auto u8 = rfs::u8string(rfs::path(HOST_ROOT "a/b"));
  // gtest 1.14 cannot print std::u8string, so compare the bytes as std::string.
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(u8.data()), u8.size()), HOST_ROOT "a/b");
}
