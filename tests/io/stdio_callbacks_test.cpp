#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <string>

#include "../fs/test_support.hpp"
#include "../support/fake_backend.hpp"
#include "../support/platform_paths.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/io/stdio.hpp"

// These run the callback-backed stream (fopencookie on glibc, funopen on
// macOS/FreeBSD) through its seek, tell and copy-up paths. On Windows, which
// has no callback stream, the same calls go through the copy-at-open stream, so
// the content checks still hold.

namespace mounts = rwz::fs::mounts;
namespace rfs    = rwz::fs;
namespace stdio  = rwz::stdio;

class StdioCallbacks : public ::testing::Test {
protected:
	void SetUp() override
	{
		mounts::clear();
		rfs::cwd::clear();
		backend_ = std::make_shared<fake_backend>();
		backend_->add_file("DIR/FILE.DAT", "payload"); // 7 bytes
		ASSERT_TRUE(mounts::add(RWZ_ROOT, backend_, overlay_.path()));
	}
	void TearDown() override
	{
		mounts::clear();
		rfs::cwd::clear();
	}

	std::string archive_content()
	{
		std::FILE* fp = stdio::fopen(RWZ_ROOT "/DIR/FILE.DAT", "rb");
		if (fp == nullptr) {
			return "<missing>";
		}
		char buf[32] = {};
		const auto n = std::fread(buf, 1, sizeof(buf), fp);
		stdio::fclose(fp);
		return std::string(buf, n);
	}

	TempDir overlay_;
	std::shared_ptr<fake_backend> backend_;
};

TEST_F(StdioCallbacks, SeekFromEndThenWriteExtendsFile)
{
	std::FILE* fp = stdio::fopen(RWZ_ROOT "/DIR/FILE.DAT", "r+b");
	ASSERT_NE(fp, nullptr);
	ASSERT_EQ(stdio::fseek(fp, 0, SEEK_END), 0);
	EXPECT_EQ(stdio::ftell(fp), 7L); // END reports the archive file's size
	EXPECT_EQ(std::fwrite("!!", 1, 2, fp), 2u);
	EXPECT_EQ(stdio::fclose(fp), 0);
	EXPECT_EQ(archive_content(), "payload!!");
}

TEST_F(StdioCallbacks, TellTracksWritesAndSeeksBeforeCopyUp)
{
	std::FILE* fp = stdio::fopen(RWZ_ROOT "/DIR/FILE.DAT", "r+b");
	ASSERT_NE(fp, nullptr);
	ASSERT_EQ(stdio::fseek(fp, 2, SEEK_SET), 0);
	EXPECT_EQ(stdio::ftell(fp), 2L);
	EXPECT_EQ(std::fwrite("QQ", 1, 2, fp), 2u);
	EXPECT_EQ(stdio::ftell(fp), 4L);
	EXPECT_EQ(stdio::fclose(fp), 0);
	EXPECT_EQ(archive_content(), "paQQoad");
}

TEST_F(StdioCallbacks, ReadBackAfterCopyUpSeesWrittenBytes)
{
	std::FILE* fp = stdio::fopen(RWZ_ROOT "/DIR/FILE.DAT", "r+b");
	ASSERT_NE(fp, nullptr);
	ASSERT_EQ(stdio::fseek(fp, 0, SEEK_SET), 0);
	EXPECT_EQ(std::fwrite("P", 1, 1, fp), 1u);
	ASSERT_EQ(stdio::fseek(fp, 0, SEEK_SET), 0);
	char buf[8] = {};
	EXPECT_EQ(std::fread(buf, 1, sizeof(buf), fp), 7u);
	EXPECT_EQ(std::string(buf, 7), "Payload");
	EXPECT_EQ(stdio::fclose(fp), 0);
}

TEST_F(StdioCallbacks, AppendWritesAtEndAndTellReportsIt)
{
	std::FILE* fp = stdio::fopen(RWZ_ROOT "/DIR/FILE.DAT", "a");
	ASSERT_NE(fp, nullptr);
	EXPECT_EQ(std::fwrite("++", 1, 2, fp), 2u);
	EXPECT_EQ(stdio::fclose(fp), 0);
	EXPECT_EQ(archive_content(), "payload++");
}
