// The high-level session interface, in both builds: the failure paths that do
// not need dwarfs.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "../fs/test_support.hpp"
#include "rwzfs/session.h"

namespace fs = std::filesystem;

TEST(SessionApi, OpeningAMissingArchiveFails)
{
	TempDir dir;
	auto s = rwz::session::open(dir.path() / "missing.dwarfs");
	ASSERT_FALSE(s.has_value());
	EXPECT_EQ(s.error(), rwz::MountError::ArchiveNotFound);
}

TEST(SessionApi, ArchiveDirectoryRejectsAMissingSource)
{
	TempDir dir;
	const auto result = rwz::archive_directory(dir.path() / "Game",
	                                           dir.path() / "no-such-dir");
	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error(), rwz::create_error::source_missing);
}

TEST(SessionApi, ArchiveDirectoryNeverOverwrites)
{
	TempDir dir;
	fs::create_directories(dir.path() / "src");
	std::ofstream(dir.path() / "src" / "a.bin") << "x";
	std::ofstream(dir.path() / "Game.dwarfs") << "existing";
	const auto result = rwz::archive_directory(dir.path() / "Game",
	                                           dir.path() / "src");
	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error(), rwz::create_error::already_exists);
	std::ifstream in(dir.path() / "Game.dwarfs");
	std::string text = {};
	in >> text;
	EXPECT_EQ(text, "existing"); // untouched
}

TEST(SessionApi, ArchiveDirectoryAddsTheExtensionOnce)
{
	TempDir dir;
	fs::create_directories(dir.path() / "src");
	std::ofstream(dir.path() / "src" / "a.bin") << "x";
	std::ofstream(dir.path() / "Game.dwarfs") << "existing";
	// The name already has the extension: it is not doubled, so the
	// existing file is found.
	const auto result = rwz::archive_directory(dir.path() / "Game.dwarfs",
	                                           dir.path() / "src");
	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error(), rwz::create_error::already_exists);
}
