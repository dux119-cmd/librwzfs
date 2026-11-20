// rwz::fs::proximate(p, ec): p relative to the current directory, on host and
// archive paths.
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

#include "../fs/test_support.hpp"
#include "../support/fake_backend.hpp"
#include "../support/platform_paths.hpp"
#include "rwzfs/fs/cwd.h"
#include "rwzfs/fs/filesystem.h"
#include "rwzfs/fs/mount_table.h"

namespace fs     = std::filesystem;
namespace mounts = rwz::fs::mounts;

TEST(Proximate, HostPathIsRelativeToTheCurrentDirectory)
{
	TempDir dir;
	const auto original = fs::current_path();
	fs::current_path(dir.path());
	std::error_code ec = {};
	const fs::path rel = rwz::fs::proximate(dir.path() / "a" / "b.txt", ec);
	fs::current_path(original);
	EXPECT_FALSE(ec);
	EXPECT_EQ(rel, fs::path("a") / "b.txt");
}

TEST(Proximate, ArchivePathIsRelativeToTheArchiveDirectory)
{
	mounts::clear();
	rwz::fs::cwd::clear();
	auto backend = std::make_shared<fake_backend>();
	backend->add_file("DIR/FILE.DAT", "payload");
	TempDir overlay;
	ASSERT_TRUE(mounts::add(RWZ_ROOT, backend, overlay.path()));
	ASSERT_TRUE(rwz::fs::cwd::set_archive_cwd(RWZ_ROOT "/DIR"));

	std::error_code ec = {};
	const fs::path rel = rwz::fs::proximate(RWZ_ROOT "/DIR/FILE.DAT", ec);
	EXPECT_FALSE(ec);
	EXPECT_EQ(rel, fs::path("FILE.DAT"));

	mounts::clear();
	rwz::fs::cwd::clear();
}
