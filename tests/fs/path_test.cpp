#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "rwzfs/fs/path.hpp"

namespace fs = std::filesystem;
namespace rfs = rwzfs::fs;

// PATH-01 .. PATH-03

TEST(PathC_Str, MatchesStandardPath) {
  const rfs::path p("/a/b");
  EXPECT_EQ(fs::path(rfs::c_str(p)), p);
}

TEST(PathString, MatchesStandardPath) {
  EXPECT_EQ(rfs::string(rfs::path("/a/b")), "/a/b");
}

TEST(PathString, EmptyPathIsEmpty) {
  EXPECT_EQ(rfs::string(rfs::path{}), "");
}

TEST(PathAppend, AddsComponentWithoutMutatingInput) {
  const rfs::path base("/a/b");
  const auto result = rfs::append(base, "c");
  EXPECT_EQ(result, fs::path("/a/b/c"));
  EXPECT_EQ(base, fs::path("/a/b"));
}

TEST(PathAppend, MatchesStandardOperatorSlash) {
  const rfs::path base("/a/b");
  EXPECT_EQ(rfs::append(base, "x/y"), base / "x/y");
  EXPECT_EQ(rfs::append(base, "/abs"), base / "/abs");
}

// PATH-04: construction

TEST(PathMake, ConstructsFromText) {
  EXPECT_EQ(rfs::make_path("/a/b.txt"), fs::path("/a/b.txt"));
}

// PATH-05: operator/

TEST(PathJoin, MatchesOperatorSlash) {
  const rfs::path lhs("/a");
  const rfs::path rhs("b/c");
  EXPECT_EQ(rfs::join(lhs, rhs), lhs / rhs);
}

// PATH-06 .. PATH-14: component queries

TEST(PathFilename, ReturnsLastComponent) {
  EXPECT_EQ(rfs::filename(rfs::path("/a/b.txt")), "b.txt");
}

TEST(PathParentPath, ReturnsDirectory) {
  EXPECT_EQ(rfs::parent_path(rfs::path("/a/b.txt")), "/a");
}

TEST(PathExtension, ReturnsSuffix) {
  EXPECT_EQ(rfs::extension(rfs::path("/a/b.tar.gz")), ".gz");
}

TEST(PathExtension, EmptyWhenNone) {
  EXPECT_EQ(rfs::extension(rfs::path("/a/README")), "");
}

TEST(PathIsAbsolute, DistinguishesAbsoluteFromRelative) {
  EXPECT_TRUE(rfs::is_absolute(rfs::path("/a")));
  EXPECT_FALSE(rfs::is_absolute(rfs::path("a")));
}

TEST(PathStem, ReturnsNameWithoutExtension) {
  EXPECT_EQ(rfs::stem(rfs::path("/a/b.tar.gz")), "b.tar");
}

TEST(PathLexicallyRelative, ComputesRelativePath) {
  EXPECT_EQ(rfs::lexically_relative(rfs::path("/a/b/c"), rfs::path("/a")),
            fs::path("b/c"));
}

TEST(PathReplaceExtension, ReplacesSuffixWithoutMutatingInput) {
  const rfs::path original("/a/b.txt");
  EXPECT_EQ(rfs::replace_extension(original, ".md"), fs::path("/a/b.md"));
  EXPECT_EQ(original, fs::path("/a/b.txt"));
}

TEST(PathRelativePath, StripsRoot) {
  EXPECT_EQ(rfs::relative_path(rfs::path("/a/b")), fs::path("a/b"));
}

TEST(PathU8String, MatchesStandardForAscii) {
  const auto u8 = rfs::u8string(rfs::path("/a/b"));
  // gtest 1.14 cannot print std::u8string, so compare the bytes as std::string.
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(u8.data()), u8.size()), "/a/b");
}
