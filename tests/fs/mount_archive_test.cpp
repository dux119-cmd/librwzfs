#include "../support/platform_paths.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "../archive/fixture.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/mount.hpp"
#include "test_support.hpp"

namespace fs     = std::filesystem;
namespace mounts = rwz::fs::mounts;

class MountArchive : public ::testing::Test {
protected:
	void SetUp() override
	{
		mounts::clear();
	}
	void TearDown() override
	{
		mounts::clear();
	}

	// Every test writes overlays under its own temporary directory, never
	// under the real home.
	rwz::mount_options options() const
	{
		rwz::mount_options o;
		o.write_root = writes_.path();
		return o;
	}

	TempDir writes_;
};

TEST_F(MountArchive, MissingArchiveIsReported)
{
	EXPECT_EQ(rwz::mount_archive(fs::path(HOST_ROOT "does/not/exist.dwarfs"),
	                             RWZ_ROOT "",
	                             options()),
	          rwz::MountError::ArchiveNotFound);
	EXPECT_FALSE(mounts::is_archive_path(RWZ_ROOT "/x"));
}

TEST_F(MountArchive, InvalidMountpointIsReportedBeforeOpening)
{
	TempDir dir;
	const auto archive = dir.path() / "garbage.dwarfs";
	std::ofstream(archive) << "not a dwarfs image";
	EXPECT_EQ(rwz::mount_archive(archive, HOST_ROOT "mnt/outside", options()),
	          rwz::MountError::MountPointInvalid);
}

TEST_F(MountArchive, GarbageImageFailsToOpen)
{
	TempDir dir;
	const auto archive = dir.path() / "garbage.dwarfs";
	std::ofstream(archive) << "not a dwarfs image";
	EXPECT_EQ(rwz::mount_archive(archive, RWZ_ROOT "", options()),
	          rwz::MountError::OpenFailed);
	EXPECT_FALSE(mounts::is_archive_path(RWZ_ROOT "/x"));
}

TEST_F(MountArchive, RelativeWriteRootIsResolvedAgainstCurrentDirectory)
{
	const auto original = fs::current_path();
	fs::current_path(writes_.path());
	rwz::mount_options o;
	o.write_root      = fs::path("relative/writes");
	const auto result = rwz::mount_archive(kFixtureArchive, RWZ_ROOT "", o);
	fs::current_path(original);
	ASSERT_EQ(result, rwz::MountError::None);
	// Resolved at mount time: the overlay lives under the directory that
	// was current then.
	EXPECT_TRUE(fs::is_directory(writes_.path() / "relative" / "writes" /
	                             "fixture-writes"));
	rwz::unmount_archive(RWZ_ROOT "");
}

TEST_F(MountArchive, RelativeArchivePathMounts)
{
	const auto original    = fs::current_path();
	const fs::path archive = kFixtureArchive;
	fs::current_path(archive.parent_path());
	const auto result = rwz::mount_archive(archive.filename(),
	                                       RWZ_ROOT "",
	                                       options());
	fs::current_path(original);
	ASSERT_EQ(result, rwz::MountError::None);
	EXPECT_TRUE(mounts::is_archive_path(RWZ_ROOT "/DIR/FILE.DAT"));
	rwz::unmount_archive(RWZ_ROOT "");
}

TEST_F(MountArchive, WriteRootBelowRegularFileIsUnavailable)
{
	const auto blocker = writes_.path() / "blocker";
	std::ofstream(blocker) << "x";
	rwz::mount_options o;
	o.write_root = blocker / "sub";
	EXPECT_EQ(rwz::mount_archive(kFixtureArchive, RWZ_ROOT "", o),
	          rwz::MountError::WriteRootUnavailable);
}

TEST_F(MountArchive, FixtureMountCreatesOverlayDirectoryNamedFromStem)
{
	ASSERT_EQ(rwz::mount_archive(kFixtureArchive, RWZ_ROOT "", options()),
	          rwz::MountError::None);
	// kFixtureArchive is ".../fixture.dwarfs", so the stem is "fixture".
	const auto expected = writes_.path() / "fixture-writes";
	EXPECT_TRUE(fs::is_directory(expected));
	const auto r = mounts::find(fs::path(RWZ_ROOT "/DIR/FILE.DAT"));
	ASSERT_TRUE(r.has_value());
	EXPECT_EQ(r->overlay, expected);
}

TEST_F(MountArchive, SecondMountOfSameArchiveIsInUse)
{
	ASSERT_EQ(rwz::mount_archive(kFixtureArchive, RWZ_ROOT "", options()),
	          rwz::MountError::None);
	EXPECT_EQ(rwz::mount_archive(kFixtureArchive, RWZ_ROOT "/second", options()),
	          rwz::MountError::ArchiveInUse);
}

TEST_F(MountArchive, UnmountReleasesLockSoRemountSucceeds)
{
	ASSERT_EQ(rwz::mount_archive(kFixtureArchive, RWZ_ROOT "", options()),
	          rwz::MountError::None);
	EXPECT_TRUE(fs::exists(writes_.path() / "fixture-writes" / ".rwz.lock"));
	EXPECT_TRUE(rwz::unmount_archive(RWZ_ROOT ""));
	EXPECT_FALSE(fs::exists(writes_.path() / "fixture-writes" / ".rwz.lock"));
	EXPECT_EQ(rwz::mount_archive(kFixtureArchive, RWZ_ROOT "", options()),
	          rwz::MountError::None);
}
