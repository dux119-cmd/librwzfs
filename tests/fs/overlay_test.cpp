#include "../support/platform_paths.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>

#include "rwzfs/overlay.hpp"

namespace fs = std::filesystem;
namespace ov = rwzfs::overlay;

namespace {

// Environment with HOME only (LOCALAPPDATA on Windows).
ov::env_lookup env_with_home(const std::string& home) {
  return [home](const char* name) -> std::optional<std::string> {
#if defined(_WIN32)
    if (std::string(name) == "LOCALAPPDATA") return home;
#else
    if (std::string(name) == "HOME") return home;
#endif
    return std::nullopt;
  };
}

ov::env_lookup env_empty() {
  return [](const char*) -> std::optional<std::string> { return std::nullopt; };
}

}  // namespace

TEST(OverlayStem, StripsExtensionFromFirstDot) {
  EXPECT_EQ(ov::archive_stem("archive.zip"), "archive");
  EXPECT_EQ(ov::archive_stem("fixture.dwarfs"), "fixture");
  EXPECT_EQ(ov::archive_stem(HOST_ROOT "data/dir/x.tar.gz"), "x");
}

TEST(OverlayStem, FirstDotRuleAppliesToMultipleDots) {
  EXPECT_EQ(ov::archive_stem("my.data.dwarfs"), "my");
}

TEST(OverlayStem, NoExtensionKeepsName) {
  EXPECT_EQ(ov::archive_stem("archive"), "archive");
}

TEST(OverlayStem, LeadingDotIsKept) {
  EXPECT_EQ(ov::archive_stem(".hidden.dwarfs"), ".hidden");
  EXPECT_EQ(ov::archive_stem(".hidden"), ".hidden");
}

TEST(OverlayDefaultRoot, UserStateAreaFromHome) {
  const auto root = ov::default_write_root(env_with_home(HOST_ROOT "home/u"));
  ASSERT_TRUE(root.has_value());
#if defined(_WIN32)
  EXPECT_EQ(*root, fs::path(HOST_ROOT "home/u") / "rwz");
#elif defined(__APPLE__)
  EXPECT_EQ(*root, fs::path(HOST_ROOT "home/u") / "Library" / "Application Support" / "rwz");
#else
  EXPECT_EQ(*root, fs::path(HOST_ROOT "home/u") / ".local" / "state" / "rwz");
#endif
}

#if !defined(_WIN32) && !defined(__APPLE__)
namespace {
ov::env_lookup env_with(std::optional<std::string> xdg, std::optional<std::string> home) {
  return [xdg, home](const char* name) -> std::optional<std::string> {
    if (std::string(name) == "XDG_STATE_HOME") return xdg;
    if (std::string(name) == "HOME") return home;
    return std::nullopt;
  };
}
}  // namespace

TEST(OverlayDefaultRoot, AbsoluteXdgStateHomeIsUsed) {
  const auto root = ov::default_write_root(env_with(HOST_ROOT "xdg/state", HOST_ROOT "home/u"));
  ASSERT_TRUE(root.has_value());
  EXPECT_EQ(*root, fs::path(HOST_ROOT "xdg/state") / "rwz");
}

TEST(OverlayDefaultRoot, RelativeXdgStateHomeIsIgnored) {
  const auto root = ov::default_write_root(env_with("relative/state", HOST_ROOT "home/u"));
  ASSERT_TRUE(root.has_value());
  EXPECT_EQ(*root, fs::path(HOST_ROOT "home/u") / ".local" / "state" / "rwz");
}

TEST(OverlayDefaultRoot, UnsetXdgStateHomeFallsBackToHome) {
  const auto root = ov::default_write_root(env_with(std::nullopt, HOST_ROOT "home/u"));
  ASSERT_TRUE(root.has_value());
  EXPECT_EQ(*root, fs::path(HOST_ROOT "home/u") / ".local" / "state" / "rwz");
}

TEST(OverlayDefaultRoot, XdgStateHomeWithoutHomeStillWorks) {
  const auto root = ov::default_write_root(env_with(HOST_ROOT "xdg/state", std::nullopt));
  ASSERT_TRUE(root.has_value());
  EXPECT_EQ(*root, fs::path(HOST_ROOT "xdg/state") / "rwz");
}
#endif

TEST(OverlayDefaultRoot, MissingHomeIsReported) {
  const auto root = ov::default_write_root(env_empty());
  ASSERT_FALSE(root.has_value());
  EXPECT_EQ(root.error(), ov::overlay_error::user_state_unknown);
}

TEST(OverlayDirectory, DefaultRootUsesStemWritesSubdirectory) {
  const auto dir = ov::overlay_directory(HOST_ROOT "a/archive.zip", std::nullopt, env_with_home(HOST_ROOT "home/u"));
  ASSERT_TRUE(dir.has_value());
  EXPECT_EQ(dir->filename(), "archive-writes");
  EXPECT_EQ(dir->parent_path(), *ov::default_write_root(env_with_home(HOST_ROOT "home/u")));
}

TEST(OverlayDirectory, SpecifiedRootIsTopLevelWritePath) {
  const auto dir = ov::overlay_directory(HOST_ROOT "data/archive.zip", fs::path(HOST_ROOT "srv/wr"), env_empty());
  ASSERT_TRUE(dir.has_value());
  EXPECT_EQ(*dir, fs::path(HOST_ROOT "srv/wr/archive-writes"));
}

TEST(OverlayDirectory, SpecifiedRootDoesNotNeedHome) {
  const auto dir = ov::overlay_directory("fixture.dwarfs", fs::path(HOST_ROOT "srv/wr"), env_empty());
  EXPECT_TRUE(dir.has_value());
}

TEST(OverlayDirectory, RelativeRootIsRejected) {
  const auto dir = ov::overlay_directory("archive.zip", fs::path("relative/wr"), env_with_home(HOST_ROOT "h"));
  ASSERT_FALSE(dir.has_value());
  EXPECT_EQ(dir.error(), ov::overlay_error::write_root_relative);
}

TEST(OverlayDirectory, DefaultWithoutHomeIsReported) {
  const auto dir = ov::overlay_directory("archive.zip", std::nullopt, env_empty());
  ASSERT_FALSE(dir.has_value());
  EXPECT_EQ(dir.error(), ov::overlay_error::user_state_unknown);
}
