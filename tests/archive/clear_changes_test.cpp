// clear_changes: removes an archive's overlay changes; refuses while the
// overlay is locked; succeeds when there is nothing to clear. Works on the
// overlay directory alone, so it runs in both builds.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "../fs/test_support.hpp"
#include "rwzfs/overlay/lock.h"
#include "rwzfs/rwz.h"

namespace fs = std::filesystem;

namespace {

void touch(const fs::path& p)
{
	fs::create_directories(p.parent_path());
	std::ofstream(p) << "x";
}

} // namespace

TEST(ClearChanges, RemovesDataWhiteoutsAndOpaqueDirectories)
{
	TempDir dir;
	rwz::session_options options = {};
	options.overlay_root         = dir.path() / "writes";
	const fs::path archive       = dir.path() / "Game.dwarfs";
	const fs::path overlay       = *rwz::overlay::overlay_directory(
	        archive, options.overlay_root, rwz::overlay::process_environment());
	touch(overlay / "data" / "save.dat");
	touch(overlay / "whiteouts" / "old.bin");
	fs::create_directories(overlay / "opaque" / "dir");

	const auto cleared = rwz::clear_changes(archive, options);
	ASSERT_TRUE(cleared.has_value()) << rwz::to_string(cleared.error());
	EXPECT_FALSE(fs::exists(overlay / "data" / "save.dat"));
	EXPECT_FALSE(fs::exists(overlay / "whiteouts" / "old.bin"));
	EXPECT_FALSE(fs::exists(overlay / "opaque" / "dir"));
	EXPECT_FALSE(fs::exists(overlay / ".rwz.lock")); // released
}

TEST(ClearChanges, RefusesWhileTheOverlayIsLocked)
{
	TempDir dir;
	rwz::session_options options = {};
	options.overlay_root         = dir.path() / "writes";
	const fs::path archive       = dir.path() / "Game.dwarfs";
	const fs::path overlay       = *rwz::overlay::overlay_directory(
	        archive, options.overlay_root, rwz::overlay::process_environment());
	touch(overlay / "data" / "save.dat");
	touch(overlay / ".rwz.lock"); // another instance holds the lock

	const auto cleared = rwz::clear_changes(archive, options);
	ASSERT_FALSE(cleared.has_value());
	EXPECT_EQ(cleared.error(), rwz::clear_error::archive_in_use);
	EXPECT_TRUE(fs::exists(overlay / "data" / "save.dat")); // nothing was
	                                                        // removed
	EXPECT_TRUE(fs::exists(overlay / ".rwz.lock")); // the other instance's
	                                                // lock is left alone
}

TEST(ClearChanges, NoOverlayIsSuccess)
{
	TempDir dir;
	rwz::session_options options = {};
	options.overlay_root         = dir.path() / "writes";
	EXPECT_TRUE(rwz::clear_changes(dir.path() / "Never.dwarfs", options).has_value());
}
