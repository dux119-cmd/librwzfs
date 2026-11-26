// extract_archive end to end (needs dwarfs): an archive with an overlay (a new
// file, a changed file, a deleted file) is copied to an empty directory; the
// copy holds the merged view. A non-empty destination is refused, untouched.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "../fs/test_support.hpp"
#include "rwzfs/rwz.h"

namespace fs = std::filesystem;

namespace {

std::string read_all(const fs::path& p)
{
	std::ifstream in(p, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in),
	                   std::istreambuf_iterator<char>());
}

} // namespace

TEST(ExtractRoundTrip, CopiesTheMergedViewToAnEmptyDirectory)
{
	TempDir dir;
	const fs::path src = dir.path() / "src";
	fs::create_directories(src / "a" / "sub");
	std::ofstream(src / "a" / "keep.txt") << "original";
	std::ofstream(src / "a" / "gone.txt") << "to be deleted";
	std::ofstream(src / "a" / "sub" / "deep.bin", std::ios::binary)
	        << std::string(100000, 'd');
	const auto made = rwz::archive_directory(dir.path() / "Game", src);
	ASSERT_TRUE(made.has_value())
	        << "create error " << static_cast<int>(made.error());

	rwz::session_options options = {};
	options.overlay_root         = dir.path() / "writes";
	{
		// Changes through the mounted view: a new file, a changed file,
		// and a deleted one. The session closes before the extraction,
		// which takes its own mount.
		auto session = rwz::session::open(*made, options);
		ASSERT_TRUE(session.has_value());
		{
			rwz::stdio::file_ostream out(session->root() / "a" / "new.txt",
			                             std::ios::out | std::ios::binary);
			out << "added";
		}
		{
			rwz::stdio::file_ostream out(session->root() / "a" / "keep.txt",
			                             std::ios::out | std::ios::binary);
			out << "changed";
		}
		std::error_code ec = {};
		ASSERT_TRUE(rwz::fs::remove(session->root() / "a" / "gone.txt", ec));
	}

	const fs::path dest = dir.path() / "out";
	const auto copied   = rwz::extract_archive(*made, dest, options);
	ASSERT_TRUE(copied.has_value()) << rwz::to_string(copied.error());
	EXPECT_EQ(copied->files, 3u); // keep.txt (changed), new.txt, sub/deep.bin
	EXPECT_EQ(copied->bytes, 7u + 5u + 100000u);
	EXPECT_EQ(read_all(dest / "a" / "keep.txt"), "changed");
	EXPECT_EQ(read_all(dest / "a" / "new.txt"), "added");
	EXPECT_EQ(read_all(dest / "a" / "sub" / "deep.bin"),
	          std::string(100000, 'd'));
	EXPECT_FALSE(fs::exists(dest / "a" / "gone.txt")); // deleted in the
	                                                   // overlay: not in
	                                                   // the merged view
}

TEST(ExtractRoundTrip, NonEmptyDestinationIsRefusedAndUntouched)
{
	TempDir dir;
	const fs::path src = dir.path() / "src";
	fs::create_directories(src);
	std::ofstream(src / "x.txt") << "x";
	const auto made = rwz::archive_directory(dir.path() / "Game", src);
	ASSERT_TRUE(made.has_value());

	const fs::path dest = dir.path() / "out";
	fs::create_directories(dest);
	std::ofstream(dest / "mine.txt") << "keep me";
	rwz::session_options options = {};
	options.overlay_root         = dir.path() / "writes";
	const auto copied = rwz::extract_archive(*made, dest, options);
	ASSERT_FALSE(copied.has_value());
	EXPECT_EQ(copied.error(), rwz::extract_error::dest_not_empty);
	EXPECT_EQ(read_all(dest / "mine.txt"), "keep me");
	EXPECT_FALSE(fs::exists(dest / "x.txt"));
}
