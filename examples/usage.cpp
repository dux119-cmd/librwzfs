// Showcase of the rwzfs library, end to end. The same read script runs against
// a host tree, a mounted archive, and the archive after changes and a revision;
// the transcripts are printed side by side and compared. The write script
// exercises overlay writes, deletes, chmod and mkdir.
//
//   rwzfs_example <work directory>
//
// Build: needs dwarfs (the archive writer and reader) and POSIX.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "rwzfs/archive/create.h"
#include "rwzfs/fs/archive_path.h"
#include "rwzfs/fs/filesystem.h"
#include "rwzfs/io/stdio.h"
#include "rwzfs/mount.h"
#include "rwzfs/revise.h"

#if defined(_WIN32)
#include <windows.h>

#include "rwzfs/io/win32.h"
#else
#include <dirent.h>

#include "rwzfs/io/posix.h"
#endif

namespace fs    = std::filesystem;
namespace stdio = rwz::stdio;

namespace {

constexpr std::size_t kNumbersBytes = 4096;
constexpr std::size_t kPreviewBytes = 40;

// A transcript: one line per operation result. Relative paths only, so host and
// archive runs match.
struct transcript {
	std::vector<std::string> lines = {};
	void add(const std::string& line)
	{
		lines.push_back(line);
	}
};

std::string join(const std::string& root, const std::string& rel)
{
	return rel.empty() ? root : root + "/" + rel;
}

std::string escape(const std::string& text)
{
	std::string out = {};
	for (const char c : text) {
		if (c == '\n') {
			out += "\\n";
		} else if (c < 0x20 || c > 0x7e) {
			out += '.';
		} else {
			out += c;
		}
	}
	return out;
}

std::string hex(const std::string& bytes)
{
	std::ostringstream os = {};
	for (const unsigned char c : bytes) {
		os << std::hex << std::setw(2) << std::setfill('0')
		   << static_cast<int>(c);
	}
	return os.str();
}

std::string error_text(int err)
{
	return std::string("error: ") + std::strerror(err);
}

// Same text on every platform: the OS messages for a missing directory differ
// between them.
const char* kNotFound = "not found";

// File size through the stdio wrappers (works for archive, overlay and host
// paths on every platform).
long long file_size(const std::string& path)
{
	std::FILE* fp = stdio::fopen(path.c_str(), "rb");
	if (fp == nullptr) {
		return -1;
	}
	stdio::fseek(fp, 0, SEEK_END);
	const long long size = stdio::ftell(fp);
	stdio::fclose(fp);
	return size;
}

// Entry names of a directory, without "." and "..". The only platform-specific
// listing.
bool list_names(const std::string& dir, std::vector<std::string>& names)
{
#if defined(_WIN32)
	WIN32_FIND_DATAW data      = {};
	const std::wstring pattern = fs::path(dir).wstring() + L"/*";
	void* find = rwz::win32::find_first_file(pattern.c_str(), &data);
	if (find == INVALID_HANDLE_VALUE) {
		return false;
	}
	do {
		const std::wstring wide = data.cFileName;
		if (wide != L"." && wide != L"..") {
			names.push_back(fs::path(data.cFileName).string());
		}
	} while (rwz::win32::find_next_file(find, &data));
	(void)rwz::win32::find_close(find);
	return true;
#else
	// The library's wrappers, not libc: they see the mounted archive as
	// well as the host.
	DIR* d = rwz::posix::opendir(dir.c_str());
	if (d == nullptr) {
		return false;
	}
	while (const struct dirent* e = rwz::posix::readdir(d)) {
		const std::string name = e->d_name;
		if (name != "." && name != "..") {
			names.push_back(name);
		}
	}
	rwz::posix::closedir(d);
	return true;
#endif
}

// ---- read script: identical for every root ---------------------------------

void list_dir(const std::string& root, const std::string& rel, transcript& t)
{
	const std::string label = "list " + (rel.empty() ? std::string("/") : rel);
	std::vector<std::string> names = {};
	if (!list_names(join(root, rel), names)) {
		t.add(label + ": " + kNotFound);
		return;
	}
	std::sort(names.begin(), names.end());
	t.add(label + ": " + std::to_string(names.size()) + " entries");
	for (const auto& name : names) {
		const std::string child = rel.empty() ? name : rel + "/" + name;
		std::error_code ec      = {};
		const bool dir = rwz::fs::is_directory(join(root, child), ec);
		const long long size = dir ? 0 : file_size(join(root, child));
		t.add("  " + name + "  " + (dir ? "dir" : "file") +
		      (dir ? "" : "  " + std::to_string(size) + " bytes"));
	}
}

void stat_file(const std::string& root, const std::string& rel, transcript& t)
{
	std::error_code ec = {};
	const auto st      = rwz::fs::status(join(root, rel), ec);
	if (ec) {
		t.add("stat " + rel + ": " + kNotFound);
		return;
	}
	std::ostringstream os = {};
	os << "stat " << rel << ": mode " << std::oct
	   << (static_cast<unsigned>(st.permissions()) & 0777) << std::dec
	   << ", " << file_size(join(root, rel)) << " bytes";
	t.add(os.str());
}

void read_file(const std::string& root, const std::string& rel, transcript& t)
{
	std::FILE* fp = stdio::fopen(join(root, rel).c_str(), "rb");
	if (fp == nullptr) {
		t.add("read " + rel + ": " + kNotFound);
		return;
	}
	std::string data = {};
	char buf[256]    = {};
	for (;;) {
		const auto n = stdio::fread(buf, 1, sizeof(buf), fp);
		if (n == 0) {
			break;
		}
		data.append(buf, n);
	}
	stdio::fclose(fp);
	t.add("read " + rel + " (" + std::to_string(data.size()) +
	      " bytes): " + escape(data.substr(0, kPreviewBytes)));
}

void seek_read(const std::string& root, const std::string& rel, long offset,
               std::size_t count, transcript& t)
{
	std::FILE* fp = stdio::fopen(join(root, rel).c_str(), "rb");
	if (fp == nullptr) {
		t.add("seek " + rel + ": " + kNotFound);
		return;
	}
	stdio::fseek(fp, offset, SEEK_SET);
	const long at = stdio::ftell(fp);
	std::string buf(count, '\0');
	const auto n = stdio::fread(buf.data(), 1, count, fp);
	stdio::fclose(fp);
	buf.resize(n);
	t.add("seek " + rel + " to " + std::to_string(at) + ", read " +
	      std::to_string(buf.size()) + ": " + hex(buf));
}

void stdio_read(const std::string& root, const std::string& rel, transcript& t)
{
	std::FILE* fp = stdio::fopen(join(root, rel).c_str(), "rb");
	if (fp == nullptr) {
		t.add("fopen " + rel + ": " + kNotFound);
		return;
	}
	char buf[16]          = {};
	const auto n          = stdio::fread(buf, 1, sizeof(buf), fp);
	const long after_read = stdio::ftell(fp);
	stdio::fseek(fp, 0, SEEK_END);
	const long size = stdio::ftell(fp);
	stdio::fclose(fp);
	t.add("fopen/fread/fseek/fclose " + rel + ": read " +
	      std::to_string(n) + " (position " + std::to_string(after_read) +
	      "), size " + std::to_string(size));
}

void stream_read(const std::string& root, const std::string& rel, transcript& t)
{
	stdio::file_istream in = stdio::input_file_stream(join(root, rel));
	if (!in.is_open()) {
		t.add("ifstream " + rel + ": not open");
		return;
	}
	std::string line = {};
	std::getline(in, line);
	t.add("ifstream " + rel + ": first line \"" + escape(line) + "\"");
}

void fs_checks(const std::string& root, transcript& t)
{
	std::error_code ec = {};
	for (const std::string rel : {"docs/readme.txt", "docs/missing.txt"}) {
		t.add("exists " + rel + ": " +
		      (rwz::fs::exists(join(root, rel), ec) ? "yes" : "no"));
	}
	t.add("is_directory data: " +
	      std::string(rwz::fs::is_directory(join(root, "data"), ec) ? "yes"
	                                                                : "no"));
}

// The read script. The same calls run on the host tree and on every mounted view.
void read_script(const std::string& root, transcript& t)
{
	list_dir(root, "", t);
	list_dir(root, "docs", t);
	list_dir(root, "data", t);
	list_dir(root, "data/sub", t);
	list_dir(root, "newdir", t);
	stat_file(root, "docs/readme.txt", t);
	stat_file(root, "data/numbers.bin", t);
	read_file(root, "docs/readme.txt", t);
	seek_read(root, "data/numbers.bin", 1000, 8, t);
	stdio_read(root, "data/sub/notes.txt", t);
	stream_read(root, "data/sub/notes.txt", t);
	fs_checks(root, t);
}

// ---- write script: runs on a mounted archive, lands in the overlay --------

void write_file(const std::string& root, const std::string& rel,
                const std::string& text, transcript& t)
{
	std::FILE* fp = stdio::fopen(join(root, rel).c_str(), "wb");
	if (fp == nullptr) {
		t.add("write " + rel + ": " + kNotFound);
		return;
	}
	const auto n = stdio::fwrite(text.data(), 1, text.size(), fp);
	stdio::fclose(fp);
	t.add("write " + rel + ": " + std::to_string(n) + " bytes");
}

void write_script(const std::string& root, transcript& t)
{
	write_file(root, "docs/new.txt", "written through the overlay\n", t);
	write_file(root, "data/sub/notes.txt", "replaced\n", t);
	{
		stdio::file_ostream out = stdio::output_file_stream(
		        join(root, "data/sub/log.txt"));
		out << "stream write\n";
		t.add("ofstream data/sub/log.txt: " +
		      std::string(out.is_open() ? "written" : "not open"));
	}
	std::error_code ec = {};
	t.add("remove docs/readme.txt: " +
	      std::string(rwz::fs::remove(join(root, "docs/readme.txt"), ec)
	                          ? "ok"
	                          : "failed"));
	rwz::fs::permissions(join(root, "data/numbers.bin"),
	                     fs::perms::owner_read | fs::perms::owner_write,
	                     ec);
	t.add("permissions data/numbers.bin owner read/write: " +
	      std::string(ec ? "failed" : "ok"));
	t.add("create_directory newdir: " +
	      std::string(rwz::fs::create_directory(join(root, "newdir"), ec)
	                          ? "ok"
	                          : "failed"));
	write_file(root, "newdir/a.txt", "in a new directory\n", t);
}

const char* mount_error_name(rwz::MountError e)
{
	switch (e) {
	case rwz::MountError::None: return "none";
	case rwz::MountError::ArchiveNotFound: return "archive not found";
	case rwz::MountError::MountPointInvalid: return "mount point invalid";
	case rwz::MountError::OpenFailed: return "archive could not be opened";
	case rwz::MountError::WriteRootInvalid:
		return "write root is not absolute";
	case rwz::MountError::WriteRootUnavailable:
		return "overlay directory could not be created";
	case rwz::MountError::ArchiveInUse:
		return "archive is in use (another process, or a stale .rwz.lock)";
	}
	return "unknown";
}

// ---- building the sample tree ----------------------------------------------

void make_tree(const fs::path& tree)
{
	fs::remove_all(tree);
	fs::create_directories(tree / "docs");
	fs::create_directories(tree / "data" / "sub");
	std::ofstream(tree / "docs" / "readme.txt") << "readme: a sample document\n";
	std::ofstream(tree / "data" / "sub" / "notes.txt")
	        << "notes line one\nnotes line two\n";
	std::ofstream numbers(tree / "data" / "numbers.bin", std::ios::binary);
	for (std::size_t i = 0; i < kNumbersBytes; ++i) {
		numbers.put(static_cast<char>(i % 251));
	}
}

// ---- presentation -----------------------------------------------------------

void print_stacked(const std::string& title, const transcript& t)
{
	std::cout << "\n== " << title << " ==\n";
	for (const auto& line : t.lines) {
		std::cout << line << '\n';
	}
}

// Two transcripts side by side. Each row is marked "=" when the two lines are
// identical.
bool print_side_by_side(const std::string& left_title, const transcript& left,
                        const std::string& right_title, const transcript& right)
{
	constexpr std::size_t kColumn = 58;
	std::cout << "\n== " << left_title << " | " << right_title << " ==\n";
	bool same = left.lines.size() == right.lines.size();
	const std::size_t rows = (std::max)(left.lines.size(), right.lines.size());
	for (std::size_t i = 0; i < rows; ++i) {
		const std::string l = i < left.lines.size() ? left.lines[i]
		                                            : std::string();
		const std::string r = i < right.lines.size() ? right.lines[i]
		                                             : std::string();
		const bool row_same = l == r;
		same                = same && row_same;
		std::string cut     = l.substr(0, kColumn);
		std::cout << std::left << std::setw(kColumn) << cut
		          << (row_same ? " = " : " ! ")
		          << (r.size() > kColumn ? r.substr(0, kColumn) : r)
		          << '\n';
	}
	std::cout << (same ? "result: identical\n" : "result: DIFFERENT\n");
	return same;
}

bool contains(const transcript& t, const std::string& needle)
{
	return std::any_of(t.lines.begin(), t.lines.end(), [&](const std::string& line) {
		return line.find(needle) != std::string::npos;
	});
}

} // namespace

int main(int argc, char** argv)
{
	if (argc != 2) {
		std::cerr << "usage: " << argv[0] << " <work directory>\n";
		return 2;
	}
	const fs::path work = argv[1];
	fs::remove_all(work);
	fs::create_directories(work);
	const auto tree    = work / "tree";
	const auto archive = work / "project.dwarfs";
	const auto writes  = work / "writes";
	const auto root    = rwz::fs::archive_root().string();
	bool ok            = true;

	// 1. Sample tree on the host, and the read script on it (pass-through:
	// no archive mounted).
	make_tree(tree);
	transcript host = {};
	read_script(tree.string(), host);
	print_stacked("1. host tree (pass-through, no archive mounted)", host);

	// 2. Archive the tree, mount it, and run the same read script on the
	// mounted view.
	rwz::create_options options = {};
	options.compression         = "zstd:level=12";
	if (!rwz::create_archive(tree, archive, options)) {
		std::cerr << "create_archive failed\n";
		return 1;
	}
	std::cout << "\narchive: " << archive << " (" << fs::file_size(archive)
	          << " bytes)\n";
	rwz::mount_options mount = {};
	mount.write_root         = writes;
	if (const auto err = rwz::mount_archive(archive, root, mount);
	    err != rwz::MountError::None) {
		std::cerr << "mount failed: " << mount_error_name(err) << "\n";
		return 1;
	}
	transcript mounted = {};
	read_script(root, mounted);
	ok = print_side_by_side("2. host tree",
	                        host,
	                        "2. mounted archive, same script",
	                        mounted) &&
	     ok;

	// 3. Writes, deletes, chmod and mkdir on the mounted archive (they land
	// in the overlay).
	transcript writes_log = {};
	write_script(root, writes_log);
	print_stacked("3. write script on the mounted archive (overlay)", writes_log);

	// 4. The read script again: the merged view now shows the changes.
	transcript changed = {};
	read_script(root, changed);

	// 5. Revise: the merged view is written to project-rev1.dwarfs, the old
	// mount is released.
	const auto revised = rwz::revise_archive(root, options);
	if (!revised) {
		std::cerr << "revise failed: error "
		          << static_cast<int>(revised.error()) << "\n";
		return 1;
	}
	std::cout << "\nrevision: " << *revised << " ("
	          << fs::file_size(*revised) << " bytes)\n";

	// 6. Mount the revision with a fresh overlay and run the read script on
	// it.
	rwz::mount_options fresh = {};
	fresh.write_root         = work / "writes-rev1";
	if (const auto err = rwz::mount_archive(*revised, root, fresh);
	    err != rwz::MountError::None) {
		std::cerr << "mount of the revision failed: "
		          << mount_error_name(err) << "\n";
		return 1;
	}
	transcript revision = {};
	read_script(root, revision);
	ok = print_side_by_side("4. changed view (overlay)",
	                        changed,
	                        "5. revised archive, same script",
	                        revision) &&
	     ok;

	// The revision must show the changes and not the deleted file.
	ok = ok && contains(revision, "new.txt") && contains(revision, "newdir") &&
	     !contains(revision, "readme.txt  file");
	rwz::unmount_archive(root);

	std::cout << (ok ? "\nall checks passed\n" : "\nCHECKS FAILED\n");
	return ok ? 0 : 1;
}
