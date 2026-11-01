#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "../support/fake_backend.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/filesystem.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/fs/resolve.hpp"

namespace fs = std::filesystem;
namespace rfs = rwzfs::fs;
namespace mounts = rwzfs::fs::mounts;

namespace {

// Archive tree (see SetUp). Chains: CHAIN_OK1..5 is five links to a file (allowed);
// CHAIN_BAD1..6 is six links (ELOOP).
class Resolve : public ::testing::Test {
 protected:
  void SetUp() override {
    mounts::clear();
    rfs::cwd::clear();
    auto b = std::make_shared<fake_backend>();
    b->add_file("DIR/FILE.DAT", "payload");
    b->add_file("DIR/SUB/inner.txt", "x");
    b->add_symlink("LINK_REL", "DIR/FILE.DAT");
    b->add_symlink("LINK_ABS", "/rwz/DIR/FILE.DAT");
    b->add_symlink("DIR/UP", "../DIR/FILE.DAT");
    b->add_symlink("DIRLINK", "DIR");
    b->add_symlink("NESTLINK", "DIR/SUB");
    b->add_symlink("DANGLING", "NOPE");
    b->add_symlink("LOOP_A", "LOOP_B");
    b->add_symlink("LOOP_B", "LOOP_A");
    b->add_symlink("HOSTLINK", "/etc/rwz-test-host");
    b->add_symlink("DIR/UPROOT", "../../hostfile");
    for (int i = 1; i <= 5; ++i) {
      const auto next = i < 5 ? "CHAIN_OK" + std::to_string(i + 1) : std::string("DIR/FILE.DAT");
      b->add_symlink("CHAIN_OK" + std::to_string(i), next);
    }
    for (int i = 1; i <= 6; ++i) {
      const auto next = i < 6 ? "CHAIN_BAD" + std::to_string(i + 1) : std::string("DIR/FILE.DAT");
      b->add_symlink("CHAIN_BAD" + std::to_string(i), next);
    }
    ASSERT_TRUE(mounts::add("/rwz", b));
  }
  void TearDown() override {
    mounts::clear();
    rfs::cwd::clear();
  }

  static fs::path resolved(const char* text) {
    std::error_code ec;
    const auto p = rfs::resolve(fs::path(text), ec);
    EXPECT_FALSE(ec) << text;
    return p;
  }
};

}  // namespace

TEST_F(Resolve, FollowsRelativeLinkAtRoot) {
  EXPECT_EQ(resolved("/rwz/LINK_REL"), fs::path("/rwz/DIR/FILE.DAT"));
}

TEST_F(Resolve, FollowsAbsoluteArchiveLink) {
  EXPECT_EQ(resolved("/rwz/LINK_ABS"), fs::path("/rwz/DIR/FILE.DAT"));
}

TEST_F(Resolve, RelativeLinkResolvesAgainstItsDirectory) {
  EXPECT_EQ(resolved("/rwz/DIR/UP"), fs::path("/rwz/DIR/FILE.DAT"));
}

TEST_F(Resolve, ChildOfDirectoryLink) {
  EXPECT_EQ(resolved("/rwz/DIRLINK/FILE.DAT"), fs::path("/rwz/DIR/FILE.DAT"));
}

TEST_F(Resolve, DotDotAfterLinkFollowsTheTargetAsTheOsDoes) {
  // NESTLINK is DIR/SUB, so NESTLINK/.. is DIR. A lexical reduction would give /rwz/FILE.DAT.
  EXPECT_EQ(resolved("/rwz/NESTLINK/../FILE.DAT"), fs::path("/rwz/DIR/FILE.DAT"));
}

TEST_F(Resolve, DanglingLinkResolvesToMissingTarget) {
  EXPECT_EQ(resolved("/rwz/DANGLING"), fs::path("/rwz/NOPE"));
  std::error_code ec;
  EXPECT_FALSE(rfs::exists(fs::path("/rwz/DANGLING"), ec));
  EXPECT_FALSE(ec);
}

TEST_F(Resolve, LoopFailsWithEloop) {
  std::error_code ec;
  (void)rfs::resolve(fs::path("/rwz/LOOP_A"), ec);
  EXPECT_EQ(ec, std::errc::too_many_symbolic_link_levels);
  EXPECT_FALSE(rfs::exists(fs::path("/rwz/LOOP_A"), ec));
  EXPECT_EQ(ec, std::errc::too_many_symbolic_link_levels);
}

TEST_F(Resolve, FiveLinksAreAllowed) {
  EXPECT_EQ(resolved("/rwz/CHAIN_OK1"), fs::path("/rwz/DIR/FILE.DAT"));
}

TEST_F(Resolve, SixthLinkFailsWithEloop) {
  std::error_code ec;
  (void)rfs::resolve(fs::path("/rwz/CHAIN_BAD1"), ec);
  EXPECT_EQ(ec, std::errc::too_many_symbolic_link_levels);
}

TEST_F(Resolve, NoFollowKeepsFinalLink) {
  std::error_code ec;
  EXPECT_EQ(rfs::resolve_no_follow(fs::path("/rwz/LINK_REL"), ec), fs::path("/rwz/LINK_REL"));
  EXPECT_FALSE(ec);
}

TEST_F(Resolve, NoFollowStillFollowsIntermediateLinks) {
  std::error_code ec;
  EXPECT_EQ(rfs::resolve_no_follow(fs::path("/rwz/DIRLINK/FILE.DAT"), ec),
            fs::path("/rwz/DIR/FILE.DAT"));
  EXPECT_FALSE(ec);
}

TEST_F(Resolve, AbsoluteHostTargetLeavesArchive) {
  EXPECT_EQ(resolved("/rwz/HOSTLINK"), fs::path("/etc/rwz-test-host"));
  EXPECT_FALSE(mounts::is_archive_path(fs::path("/etc/rwz-test-host")));
}

TEST_F(Resolve, RelativeTargetClimbingAboveRootLeavesArchive) {
  // /rwz/DIR/UPROOT -> ../../hostfile: DIR, then /rwz, then / (the host root).
  EXPECT_EQ(resolved("/rwz/DIR/UPROOT"), fs::path("/hostfile"));
}

TEST_F(Resolve, RelativePathUsesArchiveCwdAndFollowsLinks) {
  ASSERT_TRUE(rfs::cwd::set_archive_cwd("/rwz/DIR"));
  EXPECT_EQ(resolved("UP"), fs::path("/rwz/DIR/FILE.DAT"));
}

TEST_F(Resolve, RelativeHostPathUnchangedWithoutArchiveCwd) {
  rfs::cwd::clear();
  EXPECT_EQ(resolved("some/relative"), fs::path("some/relative"));
}

TEST_F(Resolve, LexicalResolveDoesNotFollowLinks) {
  EXPECT_EQ(rfs::resolve_lexical(fs::path("/rwz/LINK_REL")), fs::path("/rwz/LINK_REL"));
}

TEST_F(Resolve, StatusFollowsLinkButIsSymlinkDoesNot) {
  std::error_code ec;
  EXPECT_TRUE(rfs::is_regular_file(fs::path("/rwz/LINK_REL"), ec));
  EXPECT_FALSE(ec);
  EXPECT_TRUE(rfs::is_symlink(fs::path("/rwz/LINK_REL"), ec));
  EXPECT_FALSE(ec);
}

TEST_F(Resolve, DirectoryIteratorThroughLoopReportsEloop) {
  // Iterators report the resolution error through the error code.
  std::error_code ec;
  (void)rfs::directory_iterator(fs::path("/rwz/LOOP_A"), ec);
  EXPECT_EQ(ec, std::errc::too_many_symbolic_link_levels);
}
