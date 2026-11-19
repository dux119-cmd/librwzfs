// The rwz namespaces (rwz::posix, rwz::stdio, rwz::win32, rwz::fs) work on
// every platform, on host paths.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "../fs/test_support.hpp"
#include "rwzfs/rwz.h"

TEST(RwzNamespaces, StdioAndFsOnHostPaths)
{
	TempDir dir;
	const auto file = dir.path() / "alias.txt";
	{
		std::FILE* fp = rwz::stdio::fopen(file.string().c_str(), "wb");
		ASSERT_NE(fp, nullptr);
		EXPECT_EQ(rwz::stdio::fwrite("abc", 1, 3, fp), 3u);
		EXPECT_EQ(rwz::stdio::fclose(fp), 0);
	}
	std::error_code ec = {};
	EXPECT_TRUE(rwz::fs::exists(file, ec));
	EXPECT_FALSE(rwz::fs::exists(dir.path() / "missing.txt", ec));
	rwz::stdio::file_istream in = rwz::stdio::input_file_stream(file.string());
	std::string text = {};
	in >> text;
	EXPECT_EQ(text, "abc");
}
