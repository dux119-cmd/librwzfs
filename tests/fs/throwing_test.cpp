// The throwing forms of rwz::fs: the standard exception on host paths, and the
// same exception type for failures inside a mounted archive view.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "../fs/test_support.hpp"
#include "../support/fake_backend.hpp"
#include "../support/platform_paths.hpp"
#include "rwzfs/fs/cwd.h"
#include "rwzfs/fs/filesystem.h"
#include "rwzfs/fs/mount_table.h"

namespace fs     = std::filesystem;
namespace mounts = rwz::fs::mounts;

TEST(ThrowingForms, HostFailureThrowsStdFilesystemError)
{
	TempDir dir;
	const auto file = dir.path() / "plain.txt";
	std::ofstream(file) << "x";
	// A directory cannot be created under a regular file.
	EXPECT_THROW((void)rwz::fs::create_directories(file / "sub"),
	             fs::filesystem_error);
}

TEST(ThrowingForms, HostSuccessDoesNotThrow)
{
	TempDir dir;
	EXPECT_FALSE(rwz::fs::exists(dir.path() / "missing"));
	EXPECT_TRUE(rwz::fs::create_directories(dir.path() / "a" / "b"));
	EXPECT_TRUE(rwz::fs::is_regular_file(dir.path() / "a") == false);
}

class ThrowingArchive : public ::testing::Test {
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

	TempDir overlay_;
	std::shared_ptr<fake_backend> backend_ = {};
};

TEST_F(ThrowingArchive, SuccessfulCallsDoNotThrow)
{
	EXPECT_TRUE(rwz::fs::exists(RWZ_ROOT "/DIR/FILE.DAT"));
	EXPECT_FALSE(rwz::fs::exists(RWZ_ROOT "/DIR/MISSING"));
	EXPECT_TRUE(rwz::fs::is_regular_file(RWZ_ROOT "/DIR/FILE.DAT"));
	EXPECT_FALSE(rwz::fs::is_symlink(RWZ_ROOT "/DIR/FILE.DAT"));
	EXPECT_TRUE(rwz::fs::create_directories(RWZ_ROOT "/NEW/DIR"));
	EXPECT_TRUE(rwz::fs::absolute(RWZ_ROOT "/DIR/FILE.DAT").is_absolute());
}

TEST_F(ThrowingArchive, FailureThrowsFilesystemErrorWithOperationAndPath)
{
	// A regular archive file cannot have a child.
	try {
		(void)rwz::fs::create_directories(RWZ_ROOT "/DIR/FILE.DAT/child");
		FAIL() << "expected filesystem_error";
	} catch (const fs::filesystem_error& e) {
		EXPECT_EQ(std::string(e.what()).find("create_directories") !=
		                  std::string::npos,
		          true);
		EXPECT_TRUE(e.code());
	}
}

TEST_F(ThrowingArchive, DirectoryIteratorThrowsForAMissingDirectory)
{
	EXPECT_THROW(rwz::fs::directory_iterator(RWZ_ROOT "/NOPE"),
	             fs::filesystem_error);
	rwz::fs::directory_iterator ok(RWZ_ROOT "/DIR");
	EXPECT_FALSE(ok == rwz::fs::directory_iterator{});
}

TEST_F(ThrowingArchive, DirectoryIteratorAcceptsOptions)
{
	std::error_code ec = {};
	rwz::fs::directory_iterator it(RWZ_ROOT "/DIR",
	                               fs::directory_options::skip_permission_denied,
	                               ec);
	EXPECT_FALSE(ec);
	EXPECT_FALSE(it == rwz::fs::directory_iterator{});
	EXPECT_THROW(rwz::fs::directory_iterator(RWZ_ROOT "/NOPE",
	                                         fs::directory_options::none),
	             fs::filesystem_error);
}

TEST(ThrowingForms, HostDirectoryIteratorWithOptions)
{
	TempDir dir;
	std::ofstream(dir.path() / "f.txt") << "x";
	std::error_code ec = {};
	rwz::fs::directory_iterator it(dir.path(),
	                               fs::directory_options::skip_permission_denied,
	                               ec);
	EXPECT_FALSE(ec);
	EXPECT_FALSE(it == rwz::fs::directory_iterator{});
}

TEST(DirectoryEntryQueries, HostEntriesAnswerWithAndWithoutErrorCode)
{
	TempDir dir;
	std::ofstream(dir.path() / "f.txt") << "12345";
	fs::create_directories(dir.path() / "sub");
	for (const auto& entry : rwz::fs::directory_iterator(dir.path())) {
		if (entry.path().filename() == "f.txt") {
			EXPECT_TRUE(entry.exists());
			EXPECT_TRUE(entry.is_regular_file());
			EXPECT_FALSE(entry.is_directory());
			EXPECT_EQ(entry.file_size(), 5u);
		} else {
			EXPECT_TRUE(entry.is_directory());
		}
		std::error_code ec = {};
		EXPECT_TRUE(entry.exists(ec));
		EXPECT_FALSE(ec);
	}
}

TEST_F(ThrowingArchive, ArchiveEntriesAnswerTheSameQueries)
{
	int regular = 0;
	for (const auto& entry : rwz::fs::directory_iterator(RWZ_ROOT "/DIR")) {
		if (entry.exists() && entry.is_regular_file()) {
			++regular;
			EXPECT_EQ(entry.file_size(), 7u);
			EXPECT_FALSE(entry.is_symlink());
			EXPECT_FALSE(entry.is_other());
		}
	}
	EXPECT_EQ(regular, 1);
}
