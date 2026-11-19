// C++ streams over the stdio wrappers, on archive and overlay paths (fake
// backend, so it runs on every platform).
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "../fs/test_support.hpp"
#include "../support/fake_backend.hpp"
#include "../support/platform_paths.hpp"
#include "rwzfs/fs/cwd.h"
#include "rwzfs/fs/mount_table.h"
#include "rwzfs/io/stdio.h"

namespace fs     = std::filesystem;
namespace mounts = rwz::fs::mounts;
namespace stdio  = rwz::stdio;

class StdioStreams : public ::testing::Test {
protected:
	void SetUp() override
	{
		mounts::clear();
		rwz::fs::cwd::clear();
		backend_ = std::make_shared<fake_backend>();
		backend_->add_file("DIR/FILE.DAT", "payload");
		ASSERT_TRUE(mounts::add(RWZ_ROOT, backend_, overlay_.path()));
	}
	void TearDown() override
	{
		mounts::clear();
		rwz::fs::cwd::clear();
	}

	std::string archive_text(const char* path)
	{
		std::FILE* fp = stdio::fopen(path, "rb");
		if (fp == nullptr) {
			return "<missing>";
		}
		char buf[64] = {};
		const auto n = std::fread(buf, 1, sizeof(buf), fp);
		stdio::fclose(fp);
		return std::string(buf, n);
	}

	TempDir overlay_;
	std::shared_ptr<fake_backend> backend_ = {};
};

TEST_F(StdioStreams, InputStreamReadsArchiveFileAndSeeks)
{
	stdio::file_istream in = stdio::input_file_stream(RWZ_ROOT "/DIR/FILE.DAT");
	ASSERT_TRUE(in.is_open());
	std::string text;
	in >> text;
	EXPECT_EQ(text, "payload");
	in.clear();
	in.seekg(3);
	char buf[4] = {};
	in.read(buf, 4);
	EXPECT_EQ(std::string(buf, 4), "load");
	EXPECT_EQ(static_cast<long>(in.tellg()), 7L);
}

TEST_F(StdioStreams, InputStreamSeeksFromEnd)
{
	stdio::file_istream in = stdio::input_file_stream(RWZ_ROOT "/DIR/FILE.DAT");
	ASSERT_TRUE(in.is_open());
	in.seekg(-3, std::ios::end);
	std::string tail(3, '\0');
	in.read(tail.data(), 3);
	EXPECT_EQ(tail, "oad");
}

TEST_F(StdioStreams, InputStreamForMissingArchiveFileFails)
{
	stdio::file_istream in = stdio::input_file_stream(RWZ_ROOT "/NOPE.TXT");
	EXPECT_FALSE(in.is_open());
	EXPECT_TRUE(in.fail());
}

TEST_F(StdioStreams, OutputStreamOnArchiveFileCopiesUpAndTruncates)
{
	{
		stdio::file_ostream out = stdio::output_file_stream(
		        RWZ_ROOT "/DIR/FILE.DAT");
		ASSERT_TRUE(out.is_open());
		out << "new";
	}
	EXPECT_EQ(archive_text(RWZ_ROOT "/DIR/FILE.DAT"), "new");
	EXPECT_TRUE(fs::exists(overlay_.path() / "data" / "DIR" / "FILE.DAT"));
}

TEST_F(StdioStreams, OutputStreamCreatesNewOverlayFile)
{
	{
		stdio::file_ostream out = stdio::output_file_stream(
		        RWZ_ROOT "/DIR/NEW.TXT");
		ASSERT_TRUE(out.is_open());
		out << "added" << 7;
		out.flush();
	}
	EXPECT_EQ(archive_text(RWZ_ROOT "/DIR/NEW.TXT"), "added7");
}

TEST_F(StdioStreams, StreamsMoveWithoutLosingTheFile)
{
	stdio::file_istream a = stdio::input_file_stream(RWZ_ROOT "/DIR/FILE.DAT");
	stdio::file_istream b = std::move(a);
	EXPECT_TRUE(b.is_open());
	std::string text;
	b >> text;
	EXPECT_EQ(text, "payload");
}
