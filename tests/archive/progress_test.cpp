// Progress reporting while archives are written and extracted (needs dwarfs):
// the writer's callback reports the archive size and elapsed time, and always
// ends with a final call; the extraction reports its completion.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <vector>

#include "../fs/test_support.hpp"
#include "rwzfs/rwz.h"

namespace fs = std::filesystem;

TEST(Progress, ArchiveWriteEndsWithAFinalCall)
{
	TempDir dir;
	const fs::path src = dir.path() / "src";
	fs::create_directories(src / "sub");
	for (int i = 0; i < 5; ++i) {
		std::ofstream(src / ("f" + std::to_string(i) + ".bin"),
		              std::ios::binary)
		        << std::string(4096, 'x');
	}
	std::ofstream(src / "sub" / "deep.bin", std::ios::binary)
	        << std::string(8192, 'y');

	std::vector<rwz::create_progress> calls;
	rwz::create_options options = {};
	options.progress_interval = std::chrono::milliseconds(1); // report often,
	                                                          // so the test
	                                                          // sees
	                                                          // intermediate
	                                                          // calls
	options.on_progress = [&calls](const rwz::create_progress& p) {
		calls.push_back(p);
	};

	const auto made = rwz::archive_directory(dir.path() / "Game", src, options);
	ASSERT_TRUE(made.has_value())
	        << "create error " << static_cast<int>(made.error());
	ASSERT_FALSE(calls.empty());
	EXPECT_TRUE(calls.back().final);
	EXPECT_GT(calls.back().bytes_written, 0u); // the archive file's size
	                                           // once written
	for (std::size_t i = 0; i + 1 < calls.size(); ++i) {
		EXPECT_FALSE(calls[i].final);
	}
	// dwarfs writes to its log only for warnings and errors, so a clean
	// write leaves the stream empty: not checked.
}

TEST(Progress, ExtractionReportsItsCompletion)
{
	TempDir dir;
	const fs::path src = dir.path() / "src";
	fs::create_directories(src);
	std::ofstream(src / "a.txt") << "alpha";
	const auto made = rwz::archive_directory(dir.path() / "Game", src);
	ASSERT_TRUE(made.has_value());

	rwz::session_options options = {};
	options.overlay_root         = dir.path() / "writes";
	int calls                    = 0;
	rwz::extract_stats last      = {};
	const auto copied = rwz::extract_archive(*made,
	                                         dir.path() / "out",
	                                         options,
	                                         [&](const rwz::extract_stats& s) {
		                                         ++calls;
		                                         last = s;
	                                         });
	ASSERT_TRUE(copied.has_value());
	EXPECT_GE(calls, 1);
	EXPECT_EQ(last.files, 1u);
	EXPECT_EQ(last.bytes, 5u);
}
