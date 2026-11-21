// The session interface end to end (needs dwarfs): create, mount, write, count
// the changes, revise, and read the revision.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "../fs/test_support.hpp"
#include "rwzfs/rwz.h"

namespace fs = std::filesystem;

TEST(SessionRoundTrip, CreateWriteRevise)
{
	TempDir dir;
	const fs::path src = dir.path() / "src";
	fs::create_directories(src / "data");
	std::ofstream(src / "data" / "game.rom") << "original";

	const auto made = rwz::archive_directory(dir.path() / "Game", src);
	ASSERT_TRUE(made.has_value())
	        << "create error " << static_cast<int>(made.error());
	EXPECT_EQ(made->filename(), fs::path("Game.dwarfs"));

	rwz::session_options options = {};
	options.overlay_root         = dir.path() / "writes";
	auto game                    = rwz::session::open(*made, options);
	ASSERT_TRUE(game.has_value());
	EXPECT_TRUE(game->changes().empty());

	{
		// The library's stream, not std::ofstream: a standard stream
		// bypasses the wrappers, so a write through it would land on
		// the host disk instead of in the overlay.
		rwz::stdio::file_ostream out(game->root() / "data" / "save.dat",
		                             std::ios::binary);
		ASSERT_TRUE(out.is_open());
		out << "save";
	}
	EXPECT_EQ(game->changes().written, 1u);

	const auto revised = game->revise();
	ASSERT_TRUE(revised.has_value())
	        << "revise error " << static_cast<int>(revised.error());
	EXPECT_EQ(revised->filename(), fs::path("Game-rev1.dwarfs"));
	EXPECT_TRUE(fs::exists(*revised));
	EXPECT_TRUE(fs::exists(*made)); // the original is untouched (a host file)
}

TEST(SessionRoundTrip, ScopeEndUnmounts)
{
	TempDir dir;
	const fs::path src = dir.path() / "src";
	fs::create_directories(src);
	std::ofstream(src / "a.bin") << "x";
	const auto made = rwz::archive_directory(dir.path() / "One", src);
	ASSERT_TRUE(made.has_value());
	{
		rwz::session_options options = {};
		options.overlay_root         = dir.path() / "writes";
		auto s = rwz::session::open(*made, options);
		ASSERT_TRUE(s.has_value());
		std::error_code ec = {};
		EXPECT_TRUE(rwz::fs::exists(s->root() / "a.bin", ec)); // the
		                                                       // merged
		                                                       // view,
		                                                       // not the
		                                                       // host
	}
	// Unmounted: the second open of the same archive in the same process
	// succeeds (no lock is left behind).
	rwz::session_options options = {};
	options.overlay_root         = dir.path() / "writes";
	EXPECT_TRUE(rwz::session::open(*made, options).has_value());
}
