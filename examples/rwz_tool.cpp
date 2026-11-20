// rwz-tool: three operations on archives, with the library's own calls.
//
//   rwz-tool open   <archive> [--overlay-root DIR]
//       Mounts the archive (with its overlay) at the archive root and opens a
//       small shell on the merged view: ls, cat, stat, put, rm, mkdir. Changes
//       are kept in the overlay.
//   rwz-tool revise <archive> [--overlay-root DIR]
//       Mounts the archive with its overlay, reports what the overlay changes,
//       and writes a revised archive (<stem>-revN.dwarfs beside the original)
//       from the merged view. The original is untouched.
//   rwz-tool create <name> <source-dir> [--out-dir DIR] [--compression SPEC]
//       Archives a game directory as <out-dir>/<name>.dwarfs (default out-dir:
//       the current directory).
//
// Needs dwarfs for create and revise. The overlay of an archive is <write
// root>/<stem>-writes; the default write root is the library's
// (XDG_STATE_HOME/rwz or the platform equivalent).
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "rwzfs/fs/mount_table.h"
#include "rwzfs/rwz.h"

namespace fs = std::filesystem;

namespace {

int usage()
{
	std::cerr << "usage:\n"
	             "  rwz-tool open   <archive> [--overlay-root DIR]\n"
	             "  rwz-tool revise <archive> [--overlay-root DIR]\n"
	             "  rwz-tool create <name> <source-dir> [--out-dir DIR] [--compression SPEC]\n";
	return 2;
}

// Parses "--flag value" pairs after the positional arguments. Unknown flags are
// an error.
struct args {
	std::vector<std::string> positional    = {};
	std::optional<fs::path> overlay_root   = {};
	std::optional<fs::path> out_dir        = {};
	std::optional<std::string> compression = {};
};

std::optional<args> parse(int argc, char** argv, int first)
{
	args a = {};
	for (int i = first; i < argc; ++i) {
		const std::string arg = argv[i];
		const bool has_value  = i + 1 < argc;
		if (arg == "--overlay-root" && has_value) {
			a.overlay_root = argv[++i];
		} else if (arg == "--out-dir" && has_value) {
			a.out_dir = argv[++i];
		} else if (arg == "--compression" && has_value) {
			a.compression = argv[++i];
		} else if (arg.rfind("--", 0) == 0) {
			std::cerr << "error: unknown or incomplete option "
			          << arg << "\n";
			return std::nullopt;
		} else {
			a.positional.push_back(arg);
		}
	}
	return a;
}

rwz::mount_options mount_options_for(const args& a)
{
	rwz::mount_options options = {};
	if (a.overlay_root) {
		options.write_root = *a.overlay_root;
	}
	return options;
}

// Mounts at the archive root. Prints the reason on failure.
bool mount_root(const fs::path& archive, const args& a)
{
	const auto err = rwz::mount_archive(archive,
	                                    rwz::fs::archive_root(),
	                                    mount_options_for(a));
	if (err != rwz::MountError::None) {
		std::cerr << "error: cannot mount " << archive << ": "
		          << rwz::to_string(err) << "\n";
		return false;
	}
	return true;
}

std::string create_text(rwz::create_error e)
{
	switch (e) {
	case rwz::create_error::source_missing:
		return "the source directory is missing";
	case rwz::create_error::tool_failed:
		return "the archive writer failed (check the compression option)";
	case rwz::create_error::io: return "the output could not be written";
	case rwz::create_error::unsupported_platform:
		return "this build has no archive writer (needs dwarfs)";
	}
	return "unknown error";
}

std::string revise_text(rwz::revise_error e)
{
	switch (e) {
	case rwz::revise_error::mount_not_found: return "nothing is mounted";
	case rwz::revise_error::no_changes: return "nothing to revise";
	case rwz::revise_error::flatten_failed:
		return "the merged view could not be read";
	case rwz::revise_error::create_failed:
		return "the revised archive could not be written";
	case rwz::revise_error::io:
		return "a scratch directory could not be created";
	}
	return "unknown error";
}

// ---- open: the archive shell ----------------------------------------------

fs::path in_view(const std::string& text)
{
	const fs::path p = text;
	return p.is_absolute() ? p : rwz::fs::archive_root() / p;
}

void list(const fs::path& dir)
{
	std::vector<std::string> lines = {};
	std::error_code ec             = {};
	for (const auto& entry : rwz::fs::directory_iterator(dir, ec)) {
		const bool is_dir = entry.is_directory(ec);
		lines.push_back((is_dir ? "  dir  " : "  file ") +
		                entry.path().filename().string() +
		                (is_dir ? ""
		                        : "  " + std::to_string(entry.file_size(ec)) +
		                                  " bytes"));
	}
	if (ec) {
		std::cout << "error: " << ec.message() << "\n";
		return;
	}
	std::sort(lines.begin(), lines.end());
	for (const auto& l : lines) {
		std::cout << l << "\n";
	}
}

void cat(const fs::path& file)
{
	std::FILE* fp = rwz::stdio::fopen(file.string().c_str(), "rb");
	if (fp == nullptr) {
		std::cout << "error: cannot open " << file << "\n";
		return;
	}
	char buf[4096] = {};
	for (std::size_t n; (n = rwz::stdio::fread(buf, 1, sizeof(buf), fp)) > 0;) {
		std::cout.write(buf, static_cast<std::streamsize>(n));
	}
	rwz::stdio::fclose(fp);
}

void shell()
{
	std::cout << "mounted at " << rwz::fs::archive_root().string()
	          << ". Commands: ls [path], cat <path>, stat <path>, put <path> <text>, rm <path>, mkdir <path>, quit\n";
	std::string line = {};
	for (;;) {
		std::cout << "rwz> " << std::flush;
		if (!std::getline(std::cin, line)) {
			break;
		}
		std::istringstream in(line);
		std::string cmd = {};
		in >> cmd;
		std::string arg = {};
		in >> arg;
		if (cmd.empty()) {
			continue;
		}
		if (cmd == "quit" || cmd == "exit") {
			break;
		}
		if (cmd == "ls") {
			list(arg.empty() ? rwz::fs::archive_root() : in_view(arg));
		} else if (cmd == "cat") {
			cat(in_view(arg));
		} else if (cmd == "stat") {
			std::error_code ec = {};
			const auto p       = in_view(arg);
			if (!rwz::fs::exists(p, ec)) {
				std::cout << "not found\n";
			} else {
				const bool dir = rwz::fs::is_directory(p, ec);
				std::cout << (dir ? "directory" : "file") << "\n";
			}
		} else if (cmd == "put") {
			std::string text = {};
			std::getline(in >> std::ws, text);
			std::FILE* fp = rwz::stdio::fopen(in_view(arg).string().c_str(),
			                                  "wb");
			if (fp == nullptr) {
				std::cout << "error: cannot write " << arg << "\n";
				continue;
			}
			rwz::stdio::fwrite((text + "\n").data(), 1, text.size() + 1, fp);
			rwz::stdio::fclose(fp);
		} else if (cmd == "rm") {
			std::error_code ec = {};
			std::cout << (rwz::fs::remove(in_view(arg), ec)
			                      ? "removed\n"
			                      : "error: " + ec.message() + "\n");
		} else if (cmd == "mkdir") {
			std::error_code ec = {};
			std::cout << (rwz::fs::create_directory(in_view(arg), ec)
			                      ? "created\n"
			                      : "error: " + ec.message() + "\n");
		} else if (cmd == "help") {
			std::cout << "ls [path], cat <path>, stat <path>, put <path> <text>, rm <path>, mkdir <path>, quit\n";
		} else {
			std::cout << "unknown command: " << cmd << " (try help)\n";
		}
	}
}

int run_open(const args& a)
{
	if (a.positional.size() != 1) {
		return usage();
	}
	if (!mount_root(a.positional[0], a)) {
		return 1;
	}
	shell();
	rwz::unmount_archive(rwz::fs::archive_root());
	return 0;
}

// ---- revise: report the overlay, then write the revision --------------------

struct changes {
	std::size_t added_or_changed = 0;
	std::size_t deleted          = 0;
	std::size_t new_dirs         = 0;
};

// The overlay layout (see docs/overlay_design.md): data/ holds copied-up and
// new files, whiteouts/ marks deleted archive entries, opaque/ marks
// directories recreated over deleted ones.
changes count_overlay(const fs::path& overlay)
{
	changes c          = {};
	std::error_code ec = {};
	for (const auto& e : fs::recursive_directory_iterator(overlay / "data", ec)) {
		if (e.is_regular_file(ec)) {
			++c.added_or_changed;
		}
	}
	for (const auto& e :
	     fs::recursive_directory_iterator(overlay / "whiteouts", ec)) {
		if (e.is_regular_file(ec)) {
			++c.deleted;
		}
	}
	for (const auto& e :
	     fs::recursive_directory_iterator(overlay / "opaque", ec)) {
		if (e.is_directory(ec)) {
			++c.new_dirs;
		}
	}
	return c;
}

int run_revise(const args& a)
{
	if (a.positional.size() != 1) {
		return usage();
	}
	const fs::path archive = a.positional[0];
	if (!mount_root(archive, a)) {
		return 1;
	}

	const auto route = rwz::fs::mounts::find(rwz::fs::archive_root());
	const changes c  = route ? count_overlay(route->overlay) : changes{};
	std::cout << "overlay: " << c.added_or_changed << " file(s) written, "
	          << c.deleted << " deleted, " << c.new_dirs
	          << " directory(ies) recreated\n";
	if (c.added_or_changed + c.deleted + c.new_dirs == 0) {
		std::cout << "nothing to revise: the overlay has no changes.\n";
		rwz::unmount_archive(rwz::fs::archive_root());
		return 0;
	}

	const auto revised = rwz::revise_archive(rwz::fs::archive_root());
	rwz::unmount_archive(rwz::fs::archive_root());
	if (!revised) {
		std::cerr << "error: revision failed: "
		          << revise_text(revised.error()) << "\n";
		return 1;
	}
	std::cout << "revised archive: " << revised->string() << "\n";
	return 0;
}

// ---- create: a new archive from a game directory -------------------------------

int run_create(const args& a)
{
	if (a.positional.size() != 2) {
		return usage();
	}
	std::string name      = a.positional[0];
	const fs::path source = a.positional[1];
	if (!fs::is_directory(source)) {
		std::cerr << "error: " << source << " is not a directory\n";
		return 1;
	}
	if (name.size() < 7 || name.substr(name.size() - 7) != ".dwarfs") {
		name += ".dwarfs";
	}
	const fs::path out = a.out_dir.value_or(fs::current_path()) / name;
	if (fs::exists(out)) {
		std::cerr << "error: " << out
		          << " already exists; choose another name\n";
		return 1;
	}
	rwz::create_options options = {};
	if (a.compression) {
		options.compression = *a.compression;
	}
	const auto made = rwz::create_archive(source, out, options);
	if (!made) {
		std::cerr << "error: cannot create the archive: "
		          << create_text(made.error()) << "\n";
		return 1;
	}
	std::cout << "created " << out.string() << " (" << fs::file_size(out)
	          << " bytes) from " << source.string() << "\n";
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 2) {
		return usage();
	}
	const std::string command = argv[1];
	const auto parsed         = parse(argc, argv, 2);
	if (!parsed) {
		return usage();
	}
	if (command == "open") {
		return run_open(*parsed);
	}
	if (command == "revise") {
		return run_revise(*parsed);
	}
	if (command == "create") {
		return run_create(*parsed);
	}
	return usage();
}
