#include <gtest/gtest.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "../fs/test_support.hpp"
#include "../support/platform_paths.hpp"
#include "rwzfs/archive/create.hpp"
#include "rwzfs/archive/create_view.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/io/posix.hpp"
#include "rwzfs/mount.hpp"

namespace fs     = std::filesystem;
namespace mounts = rwz::fs::mounts;
namespace posix  = rwz::posix;

namespace {

// What a snapshot records for each entry: kind, content (files), permission
// bits and mtime.
struct node_info {
	bool dir{false};
	std::string content;
	unsigned mode{0};
	long long mtime{0};
	unsigned uid{0};
	unsigned gid{0};
	bool operator==(const node_info&) const = default;
};

// Relative path -> node_info.
using snapshot_t = std::map<std::string, node_info>;

std::string read_content(const std::string& path)
{
	const int fd = posix::open(path.c_str(), O_RDONLY, 0);
	if (fd < 0) {
		return "<unreadable>";
	}
	std::string out;
	char buf[4096];
	for (;;) {
		const auto n = posix::read(fd, buf, sizeof(buf));
		if (n <= 0) {
			break;
		}
		out.append(buf, static_cast<std::size_t>(n));
	}
	posix::close(fd);
	return out;
}

void walk(const std::string& root, const std::string& rel, snapshot_t& out)
{
	const auto dir = rel.empty() ? root : root + "/" + rel;
	DIR* d         = posix::opendir(dir.c_str());
	if (d == nullptr) {
		return;
	}
	while (const struct dirent* e = posix::readdir(d)) {
		const std::string name = e->d_name;
		if (name == "." || name == "..") {
			continue;
		}
		const auto child = rel.empty() ? name : rel + "/" + name;
		const auto full  = root + "/" + child;
		struct stat st{};
		if (posix::stat(full.c_str(), &st) != 0) {
			continue;
		}
		node_info info;
		info.mode  = st.st_mode & 07777;
		info.uid   = st.st_uid;
		info.gid   = st.st_gid;
		info.mtime = static_cast<long long>(st.st_mtime);
		if (S_ISDIR(st.st_mode)) {
			info.dir   = true;
			out[child] = info;
			walk(root, child, out);
		} else {
			info.content = read_content(full);
			out[child]   = info;
		}
	}
	posix::closedir(d);
}

snapshot_t snapshot(const std::string& root)
{
	snapshot_t out;
	walk(root, "", out);
	return out;
}

void write_file(const std::string& path, const std::string& text, int flags)
{
	const int fd = posix::open(path.c_str(), flags, 0644);
	ASSERT_GE(fd, 0) << path;
	posix::write(fd, text.data(), text.size());
	posix::close(fd);
}

} // namespace

class CreateView : public ::testing::Test {
protected:
	void SetUp() override
	{
		mounts::clear();
		rwz::fs::cwd::clear();
		fs::create_directories(src_.path() / "DIR" / "SUB");
		std::ofstream(src_.path() / "DIR" / "FILE.DAT") << "payload";
		std::ofstream(src_.path() / "DIR" / "SUB" / "inner.txt") << "x";
		std::ofstream(src_.path() / "readme.txt") << "readme\n";
	}
	void TearDown() override
	{
		mounts::clear();
		rwz::fs::cwd::clear();
	}

	// Level-only zstd: the full default (wlog, strat) needs a zstd build
	// that exposes those parameters, which not every environment has. The
	// archive is the same shape either way.
	rwz::create_options options() const
	{
		rwz::create_options o;
		o.compression = "zstd:level=12";
		return o;
	}

	TempDir src_;
	TempDir work_;
};

TEST_F(CreateView, MergedViewRoundTripsThroughNewArchive)
{
	const auto archive = work_.path() / "project.dwarfs";
	ASSERT_TRUE(rwz::create_archive(src_.path(), archive, options()).has_value());

	rwz::mount_options first;
	first.write_root = work_.path() / "writes1";
	ASSERT_EQ(rwz::mount_archive(archive, RWZ_ROOT, first),
	          rwz::MountError::None);

	// Changes in the overlay: a new file, a modified file with a new mode,
	// a deleted file.
	write_file(RWZ_ROOT "/DIR/NEW.TXT", "added", O_WRONLY | O_CREAT | O_TRUNC);
	write_file(RWZ_ROOT "/DIR/FILE.DAT", "changed", O_WRONLY | O_TRUNC);
	ASSERT_EQ(posix::chmod(RWZ_ROOT "/DIR/FILE.DAT", 0640), 0);
	ASSERT_EQ(posix::unlink(RWZ_ROOT "/readme.txt"), 0);

	const auto before = snapshot(RWZ_ROOT);
	ASSERT_EQ(before.at("DIR/NEW.TXT").content, "added");
	ASSERT_EQ(before.at("DIR/FILE.DAT").mode, 0640u);

	const auto view = work_.path() / "view.dwarfs";
	std::ostringstream diag; // writer and exception text, shown only if the
	                         // creator fails
	const auto made = rwz::create_archive_from_view(RWZ_ROOT, view, options(), &diag);
	ASSERT_TRUE(made.has_value()) << "create_archive_from_view failed: error "
	                              << static_cast<int>(made.error()) << "\n"
	                              << diag.str();
	EXPECT_TRUE(fs::exists(view));
	EXPECT_FALSE(fs::exists(view.string() + ".tmp"));

	rwz::unmount_archive(RWZ_ROOT);

	rwz::mount_options second;
	second.write_root = work_.path() / "writes2";
	ASSERT_EQ(rwz::mount_archive(view, RWZ_ROOT, second), rwz::MountError::None);
	const auto after = snapshot(RWZ_ROOT);
	rwz::unmount_archive(RWZ_ROOT);

	EXPECT_EQ(before, after);
	EXPECT_EQ(after.count("readme.txt"), 0u);
	EXPECT_EQ(after.at("DIR/FILE.DAT").content, "changed");
	EXPECT_EQ(after.at("DIR/FILE.DAT").mode, 0640u);
	EXPECT_EQ(after.at("DIR/SUB/inner.txt").content, "x");
}

TEST_F(CreateView, CreateFromUnmountedRootIsSourceMissing)
{
	const auto out = work_.path() / "never.dwarfs";
	const auto made = rwz::create_archive_from_view(RWZ_ROOT, out, options());
	ASSERT_FALSE(made.has_value());
	EXPECT_EQ(made.error(), rwz::create_error::source_missing);
	EXPECT_FALSE(fs::exists(out));
}
