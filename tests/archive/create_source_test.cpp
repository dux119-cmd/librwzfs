#include <gtest/gtest.h>

#include <filesystem>

#include "../fs/test_support.hpp"
#include "rwzfs/archive/create.h"

namespace fs = std::filesystem;

TEST(CreateArchive, MissingSourceIsReported)
{
	TempDir dir;
	const auto result = rwz::create_archive(dir.path() / "nope",
	                                        dir.path() / "out.dwarfs");
	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error(), rwz::create_error::source_missing);
	EXPECT_FALSE(fs::exists(dir.path() / "out.dwarfs"));
}

TEST(CreateArchive, DefaultCompressionMatchesCliSpec)
{
	EXPECT_STREQ(rwz::kDefaultCompression, "zstd:level=12:wlog=27:strat=3");
}
