// extract_archive: the error paths that do not need dwarfs (the archive is
// missing). The copy itself is in extract_round_trip_test.cpp (needs dwarfs).
#include <gtest/gtest.h>

#include "../fs/test_support.hpp"
#include "rwzfs/rwz.h"

TEST(ExtractArchive, MissingArchiveIsSourceMissing)
{
	TempDir dir;
	const auto result = rwz::extract_archive(dir.path() / "Absent.dwarfs",
	                                         dir.path() / "out");
	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error(), rwz::extract_error::source_missing);
	EXPECT_FALSE(std::filesystem::exists(dir.path() / "out")); // nothing was
	                                                           // created
}

TEST(ExtractArchive, EveryErrorHasText)
{
	for (auto e : {rwz::extract_error::source_missing,
	               rwz::extract_error::archive_in_use,
	               rwz::extract_error::dest_unavailable,
	               rwz::extract_error::dest_not_empty,
	               rwz::extract_error::io}) {
		EXPECT_FALSE(rwz::to_string(e).empty());
	}
}
