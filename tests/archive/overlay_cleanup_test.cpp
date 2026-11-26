// The overlay's lifetime (needs dwarfs): an archive that is mounted and closed
// with no writes leaves no overlay directory; one with writes keeps its
// overlay, so the writes survive.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "../fs/test_support.hpp"
#include "rwzfs/rwz.h"

namespace fs = std::filesystem;

namespace {

fs::path overlay_of(const fs::path& archive, const fs::path& overlay_root)
{
	return *rwz::overlay::overlay_directory(archive,
	                                        overlay_root,
	                                        rwz::overlay::process_environment());
}

} // namespace

TEST(OverlayCleanup, UnwrittenArchiveLeavesNoOverlayDirectory)
{
	TempDir dir;
	const fs::path src = dir.path() / "src";
	fs::create_directories(src);
	std::ofstream(src / "game.exe") << "exe";
	const auto made = rwz::archive_directory(dir.path() / "Game", src);
	ASSERT_TRUE(made.has_value());

	rwz::session_options options = {};
	options.overlay_root         = dir.path() / "writes";
	{
		auto session = rwz::session::open(*made, options);
		ASSERT_TRUE(session.has_value());
		EXPECT_TRUE(fs::exists(overlay_of(*made, *options.overlay_root))); // the lock file, while mounted
	} // closed: no writes
	EXPECT_FALSE(fs::exists(overlay_of(*made, *options.overlay_root)));
}

TEST(OverlayCleanup, WrittenArchiveKeepsItsOverlay)
{
	TempDir dir;
	const fs::path src = dir.path() / "src";
	fs::create_directories(src);
	std::ofstream(src / "game.exe") << "exe";
	const auto made = rwz::archive_directory(dir.path() / "Game", src);
	ASSERT_TRUE(made.has_value());

	rwz::session_options options = {};
	options.overlay_root         = dir.path() / "writes";
	{
		auto session = rwz::session::open(*made, options);
		ASSERT_TRUE(session.has_value());
		rwz::stdio::file_ostream out(session->root() / "save.dat",
		                             std::ios::out | std::ios::binary);
		out << "progress";
	} // closed: a write is saved, so the overlay stays
	EXPECT_TRUE(fs::exists(overlay_of(*made, *options.overlay_root) /
	                       "data" / "save.dat"));
}
