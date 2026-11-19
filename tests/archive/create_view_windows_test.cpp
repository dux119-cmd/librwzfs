// Windows round trip for the archive creator: build an archive, mount it,
// change the overlay, write the merged view as a new archive, mount that, and
// compare the two merged views. Uses the library's Win32 layer for listing and
// reading (the POSIX test suite does not build on Windows). Compares names,
// kinds, contents and last-write times (in whole seconds, as the archive stores
// them). Permissions are not compared: Windows has no POSIX permission bits.
#include <windows.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "rwzfs/archive/create.h"
#include "rwzfs/archive/create_view.h"
#include "rwzfs/fs/cwd.h"
#include "rwzfs/fs/mount_table.h"
#include "rwzfs/io/win32.h"
#include "rwzfs/mount.h"
#include "rwzfs/revise.h"

namespace fs     = std::filesystem;
namespace w32    = rwz::win32;
namespace mounts = rwz::fs::mounts;

namespace {

constexpr const wchar_t* kRoot    = L"C:/RWZ";
constexpr const char* kRootNarrow = "C:/RWZ";

struct node {
	bool dir{false};
	std::string content;
	std::uint64_t size{0}; // size as the directory listing reports it
	std::int64_t mtime{0}; // unix seconds
	bool operator==(const node&) const = default;
};

using snapshot_t = std::map<std::string, node>;

std::int64_t unix_seconds(const FILETIME& ft)
{
	constexpr std::int64_t kEpochDiff = 11644473600LL;
	const auto ticks = (static_cast<std::int64_t>(ft.dwHighDateTime) << 32) |
	                   static_cast<std::int64_t>(ft.dwLowDateTime);
	return ticks / 10000000LL - kEpochDiff;
}

// UTF-8 for names and contents: a plain char copy of wchar_t would truncate
// non-ASCII names.
std::string to_utf8(const std::wstring& w)
{
	if (w.empty()) {
		return {};
	}
	const int len = WideCharToMultiByte(CP_UTF8,
	                                    0,
	                                    w.data(),
	                                    static_cast<int>(w.size()),
	                                    nullptr,
	                                    0,
	                                    nullptr,
	                                    nullptr);
	std::string out(static_cast<std::size_t>(len), '\0');
	WideCharToMultiByte(CP_UTF8,
	                    0,
	                    w.data(),
	                    static_cast<int>(w.size()),
	                    out.data(),
	                    len,
	                    nullptr,
	                    nullptr);
	return out;
}

std::string read_file_text(const std::wstring& path)
{
	void* h = w32::create_file(path.c_str(),
	                           GENERIC_READ,
	                           FILE_SHARE_READ,
	                           nullptr,
	                           OPEN_EXISTING,
	                           0,
	                           nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		return "<unreadable>";
	}
	std::string out;
	char buf[4096];
	for (;;) {
		std::uint32_t n = 0;
		if (!w32::read_file(h, buf, sizeof(buf), &n, nullptr) || n == 0) {
			break;
		}
		out.append(buf, n);
	}
	EXPECT_TRUE(w32::close_handle(h));
	return out;
}

void walk(const std::string& rel, snapshot_t& out)
{
	const std::wstring dir = std::wstring(kRoot) +
	                         (rel.empty() ? L""
	                                      : L"/" + std::wstring(rel.begin(),
	                                                            rel.end()));
	WIN32_FIND_DATAW data{};
	void* h = w32::find_first_file((dir + L"/*").c_str(), &data);
	if (h == INVALID_HANDLE_VALUE) {
		return;
	}
	do {
		const std::wstring name = data.cFileName;
		if (name == L"." || name == L"..") {
			continue;
		}
		const std::string child = rel.empty() ? to_utf8(name)
		                                      : rel + "/" + to_utf8(name);
		node n;
		n.dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		n.mtime = unix_seconds(data.ftLastWriteTime);
		n.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) |
		         data.nFileSizeLow;
		if (n.dir) {
			out[child] = n;
			walk(child, out);
		} else {
			n.content  = read_file_text(dir + L"/" + name);
			out[child] = n;
		}
	} while (w32::find_next_file(h, &data));
	EXPECT_TRUE(w32::find_close(h));
}

snapshot_t snapshot()
{
	snapshot_t out;
	walk("", out);
	return out;
}

void write_text(const std::wstring& path, const std::string& text, DWORD disposition)
{
	void* h = w32::create_file(
	        path.c_str(), GENERIC_WRITE, 0, nullptr, disposition, 0, nullptr);
	ASSERT_NE(h, INVALID_HANDLE_VALUE)
	        << "create_file failed for " << to_utf8(path);
	DWORD written = 0;
	EXPECT_TRUE(::WriteFile(static_cast<HANDLE>(h),
	                        text.data(),
	                        static_cast<DWORD>(text.size()),
	                        &written,
	                        nullptr));
	EXPECT_TRUE(w32::close_handle(h));
}

} // namespace

class CreateViewWindows : public ::testing::Test {
protected:
	void SetUp() override
	{
		mounts::clear();
		rwz::fs::cwd::clear();
		fs::remove_all(work_);
		fs::create_directories(work_);
	}
	void TearDown() override
	{
		rwz::unmount_archive(kRootNarrow);
		mounts::clear();
		rwz::fs::cwd::clear();
		std::error_code ec;
		fs::remove_all(work_, ec);
	}

	static rwz::create_options options()
	{
		rwz::create_options o;
		o.compression = "zstd:level=12"; // level-only: see docs/USAGE.md
		return o;
	}

	fs::path work_ = fs::temp_directory_path() /
	                 ("rwz_winview_" + std::to_string(GetCurrentProcessId()));
};

TEST_F(CreateViewWindows, MergedViewRoundTripsThroughNewArchive)
{
	const auto src = work_ / "src";
	fs::create_directories(src / "DIR" / "SUB");
	std::ofstream(src / "DIR" / "FILE.DAT") << "payload";
	std::ofstream(src / "DIR" / "SUB" / "inner.txt") << "x";
	std::ofstream(src / "readme.txt") << "readme\n";

	const auto archive = work_ / "project.dwarfs";
	ASSERT_TRUE(rwz::create_archive(src, archive, options()).has_value());

	rwz::mount_options first;
	first.write_root = work_ / "writes1";
	ASSERT_EQ(rwz::mount_archive(archive, kRootNarrow, first),
	          rwz::MountError::None);

	write_text(std::wstring(kRoot) + L"/DIR/NEW.TXT", "added", CREATE_ALWAYS);
	write_text(std::wstring(kRoot) + L"/DIR/FILE.DAT", "changed", OPEN_EXISTING);
	EXPECT_TRUE(w32::delete_file((std::wstring(kRoot) + L"/readme.txt").c_str()));

	const auto before = snapshot();
	ASSERT_EQ(before.at("DIR/NEW.TXT").content, "added");
	ASSERT_EQ(before.at("DIR/FILE.DAT").content, "changed");

	const auto view = work_ / "view.dwarfs";
	std::ostringstream diag;
	const auto made = rwz::create_archive_from_view(kRootNarrow,
	                                                view,
	                                                options(),
	                                                &diag);
	// The writer's messages are shown with any failure below.
	SCOPED_TRACE("writer diagnostics:\n" + diag.str());
	ASSERT_TRUE(made.has_value()) << "error " << static_cast<int>(made.error());
	EXPECT_TRUE(fs::exists(view));
	EXPECT_FALSE(fs::exists(view.string() + ".tmp"));

	rwz::unmount_archive(kRootNarrow);

	rwz::mount_options second;
	second.write_root = work_ / "writes2";
	ASSERT_EQ(rwz::mount_archive(view, kRootNarrow, second),
	          rwz::MountError::None);
	const auto after = snapshot();
	rwz::unmount_archive(kRootNarrow);

	for (const auto& [name, n] : before) {
		if (!n.dir) {
			EXPECT_EQ(after.at(name).size, n.size)
			        << name << ": size in the new archive's listing";
		}
	}
	// Modification times: the archive stores whole seconds and the Windows
	// host path can differ by about a second, so compare within a tolerance
	// rather than exactly.
	constexpr std::int64_t kTimeToleranceSeconds = 5;
	for (const auto& [name, n] : before) {
		const auto delta = after.at(name).mtime - n.mtime;
		EXPECT_LE(std::abs(delta), kTimeToleranceSeconds)
		        << name << ": modification time";
	}
	// Everything else must match exactly: kinds, names, contents and sizes.
	auto strip_time = [](snapshot_t m) {
		for (auto& [name, n] : m) {
			n.mtime = 0;
		}
		return m;
	};
	EXPECT_EQ(strip_time(before), strip_time(after));
	EXPECT_EQ(after.at("DIR/FILE.DAT").content, "changed");
	EXPECT_EQ(after.at("DIR/SUB/inner.txt").content, "x");
	EXPECT_EQ(after.count("readme.txt"), 0u); // deleted in the overlay, so
	                                          // absent from the view
}

TEST_F(CreateViewWindows, RevisionWritesNextArchiveFromTheMergedView)
{
	const auto src = work_ / "src";
	fs::create_directories(src / "DIR");
	std::ofstream(src / "DIR" / "FILE.DAT") << "payload";
	const auto archive = work_ / "project.dwarfs";
	ASSERT_TRUE(rwz::create_archive(src, archive, options()).has_value());

	rwz::mount_options first;
	first.write_root = work_ / "writes1";
	ASSERT_EQ(rwz::mount_archive(archive, kRootNarrow, first),
	          rwz::MountError::None);
	write_text(std::wstring(kRoot) + L"/DIR/NEW.TXT", "added", CREATE_ALWAYS);

	const auto revised = rwz::revise_archive(kRootNarrow, options());
	ASSERT_TRUE(revised.has_value())
	        << "revise error " << static_cast<int>(revised.error());
	EXPECT_EQ(revised->filename().string(), "project-rev1.dwarfs");
	EXPECT_TRUE(fs::exists(*revised));

	rwz::mount_options second;
	second.write_root = work_ / "writes2";
	ASSERT_EQ(rwz::mount_archive(*revised, kRootNarrow, second),
	          rwz::MountError::None);
	EXPECT_EQ(read_file_text(std::wstring(kRoot) + L"/DIR/NEW.TXT"), "added");
	EXPECT_EQ(read_file_text(std::wstring(kRoot) + L"/DIR/FILE.DAT"), "payload");
}
