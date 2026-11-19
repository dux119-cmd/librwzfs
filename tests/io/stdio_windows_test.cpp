// Windows: C stdio on read-only archive files. Each open copies the file to a
// temporary file that fclose removes. Runs only on Windows.
#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include "../fs/test_support.hpp"
#include "../support/fake_backend.hpp"
#include "rwzfs/fs/cwd.h"
#include "rwzfs/fs/mount_table.h"
#include "rwzfs/io/stdio.h"

#if defined(_WIN32)

namespace fs     = std::filesystem;
namespace mounts = rwz::fs::mounts;
namespace stdio  = rwz::stdio;

namespace {

// Number of rwzfs temporary copies in the temp directory.
std::size_t copy_count()
{
	std::error_code ec{};
	std::size_t n = 0;
	for (const auto& e : fs::directory_iterator(fs::temp_directory_path(ec), ec)) {
		if (e.path().filename().string().rfind("rwz-", 0) == 0) {
			++n;
		}
	}
	return n;
}

} // namespace

class StdioWindows : public ::testing::Test {
protected:
	void SetUp() override
	{
		mounts::clear();
		rwz::fs::cwd::clear();
		backend_ = std::make_shared<fake_backend>();
		backend_->add_file("DIR/FILE.DAT", "payload");
		ASSERT_TRUE(mounts::add("C:/RWZ", backend_, overlay_.path()));
	}
	void TearDown() override
	{
		mounts::clear();
		rwz::fs::cwd::clear();
	}

	TempDir overlay_;
	std::shared_ptr<fake_backend> backend_ = {};
};

TEST_F(StdioWindows, ReadsArchiveFileAndSeeks)
{
	std::FILE* fp = stdio::fopen("C:/RWZ/DIR/FILE.DAT", "rb");
	ASSERT_NE(fp, nullptr);
	char buf[8] = {};
	EXPECT_EQ(std::fread(buf, 1, 7, fp), 7u);
	EXPECT_EQ(std::string(buf, 7), "payload");
	ASSERT_EQ(stdio::fseek(fp, 3, SEEK_SET), 0);
	EXPECT_EQ(std::fread(buf, 1, 4, fp), 4u);
	EXPECT_EQ(std::string(buf, 4), "load");
	ASSERT_EQ(stdio::fseek(fp, 0, SEEK_END), 0);
	EXPECT_EQ(stdio::ftell(fp), 7L);
	EXPECT_EQ(stdio::fclose(fp), 0);
}

TEST_F(StdioWindows, ArchiveStreamRejectsWrites)
{
	std::FILE* fp = stdio::fopen("C:/RWZ/DIR/FILE.DAT", "rb");
	ASSERT_NE(fp, nullptr);
	EXPECT_EQ(std::fwrite("zz", 1, 2, fp), 0u); // read-only copy
	EXPECT_EQ(stdio::fclose(fp), 0);
}

TEST_F(StdioWindows, MissingArchiveFileIsEnoent)
{
	errno = 0;
	EXPECT_EQ(stdio::fopen("C:/RWZ/NOPE.TXT", "rb"), nullptr);
	EXPECT_EQ(errno, ENOENT);
}

TEST_F(StdioWindows, FcloseRemovesTheTemporaryCopy)
{
	const auto before = copy_count();
	std::FILE* fp     = stdio::fopen("C:/RWZ/DIR/FILE.DAT", "rb");
	ASSERT_NE(fp, nullptr);
	EXPECT_EQ(copy_count(), before + 1);
	EXPECT_EQ(stdio::fclose(fp), 0);
	EXPECT_EQ(copy_count(), before);
}

#endif // defined(_WIN32)
