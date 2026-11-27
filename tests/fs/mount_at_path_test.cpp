// An archive mounted at a host path (c.dwarfs mounted at c): the mount shadows
// the host directory c/, relative lookups climb out of it to the host, and the
// host around it is unchanged. Uses a fake archive, so it runs in both builds.
#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "../fs/test_support.hpp"
#include "../support/fake_backend.hpp"
#include "rwzfs/fs/cwd.h"
#include "rwzfs/fs/mount_table.h"
#include "rwzfs/io/stdio.h"
#include "rwzfs/rwz.h"

namespace fs = std::filesystem;

namespace {

std::string read_through(const fs::path& p)
{
	std::FILE* f = rwz::stdio::fopen(p.string().c_str(), "rb");
	if (f == nullptr) {
		return "<missing>";
	}
	std::string out(64, '\0');
	const auto n = rwz::stdio::fread(out.data(), 1, out.size(), f);
	rwz::stdio::fclose(f);
	return out.substr(0, n);
}

} // namespace

class MountAtPath : public ::testing::Test {
protected:
	void SetUp() override
	{
		// The host: /<tmp>/a/log.txt, and a host directory c/ that the
		// mount will shadow.
		host_ = dir_.path() / "a";
		fs::create_directories(host_ / "c");
		std::ofstream(host_ / "log.txt") << "host log";
		std::ofstream(host_ / "c" / "host_only.txt") << "shadowed";

		auto backend = std::make_shared<fake_backend>();
		backend->add_file("file.txt", "archive file");
		backend->add_file("sub/deep.txt", "archive deep");
		mountpoint_ = host_ / "c";
		ASSERT_TRUE(rwz::fs::mounts::add(mountpoint_, backend, overlay_.path()));
		std::error_code ec = {};
		rwz::fs::current_path(mountpoint_, ec);
		ASSERT_FALSE(ec);
	}
	void TearDown() override
	{
		rwz::fs::mounts::clear();
		rwz::fs::cwd::clear();
	}
	TempDir dir_;
	TempDir overlay_;
	fs::path host_;
	fs::path mountpoint_;
};

TEST_F(MountAtPath, ArchiveContentIsSeenAtTheHostPath)
{
	EXPECT_EQ(read_through(host_ / "c" / "file.txt"), "archive file");
	EXPECT_EQ(read_through(host_ / "c" / "sub" / "deep.txt"), "archive deep");
}

TEST_F(MountAtPath, MountShadowsTheHostDirectory)
{
	std::error_code ec = {};
	EXPECT_FALSE(rwz::fs::exists(host_ / "c" / "host_only.txt", ec)); // the
	                                                                  // host
	                                                                  // file
	                                                                  // is
	                                                                  // hidden
	                                                                  // by
	                                                                  // the
	                                                                  // mount
	EXPECT_EQ(read_through(host_ / "c" / "host_only.txt"), "<missing>");
}

TEST_F(MountAtPath, HostFilesOutsideTheMountAreUnchanged)
{
	EXPECT_EQ(read_through(host_ / "log.txt"), "host log");
}

TEST_F(MountAtPath, RelativeLookupClimbsOutOfTheMountToTheHost)
{
	// The working directory is inside the archive (a/c); "../log.txt" is
	// a/log.txt on the host.
	EXPECT_EQ(read_through(fs::path("../log.txt")), "host log");
}

TEST_F(MountAtPath, RelativeLookupThatReEntersTheMountIsArchive)
{
	// Up to the host, then back into c/: the archive file.
	EXPECT_EQ(read_through(fs::path("../c/file.txt")), "archive file");
	EXPECT_EQ(read_through(fs::path("sub/../file.txt")), "archive file");
}

TEST_F(MountAtPath, AMountAtTheFilesystemRootIsRefused)
{
	EXPECT_FALSE(rwz::fs::mounts::is_valid_mountpoint(fs::path("/")));
	EXPECT_TRUE(rwz::fs::mounts::is_valid_mountpoint(host_ / "c"));
}

TEST_F(MountAtPath, ASecondMountAtTheSamePathIsRefused)
{
	auto other = std::make_shared<fake_backend>();
	EXPECT_FALSE(rwz::fs::mounts::add(mountpoint_, other, overlay_.path()));
}
