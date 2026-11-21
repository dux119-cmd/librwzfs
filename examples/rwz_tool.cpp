// rwz-tool: open, revise and create archives from the command line. Each
// subcommand is a few calls to the library's session interface
// (include/rwzfs/session.h).
//
//   rwz-tool open   <archive> [--overlay-root DIR]   mount, then a shell on the
//   merged view rwz-tool revise <archive> [--overlay-root DIR]   report the
//   overlay's changes, write a revision rwz-tool create <name> <source-dir>
//   [--out-dir DIR] [--compression SPEC]
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

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

rwz::session_options session_options_for(const args& a)
{
	rwz::session_options options = {};
	options.overlay_root         = a.overlay_root;
	return options;
}

std::optional<rwz::session> open_or_report(const fs::path& archive, const args& a)
{
	auto session = rwz::session::open(archive, session_options_for(a));
	if (!session) {
		std::cerr << "error: cannot mount " << archive << ": "
		          << rwz::to_string(session.error()) << "\n";
		return std::nullopt;
	}
	return std::move(*session);
}

// ---- open: a shell on the merged view ------------------------------------------

fs::path in_view(const std::string& text, const fs::path& root)
{
	const fs::path p = text;
	return p.is_absolute() ? p : root / p;
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

void shell(const fs::path& root)
{
	std::cout << "mounted at " << root.string()
	          << ". Commands: ls [path], cat <path>, stat <path>, put <path> <text>, rm <path>, mkdir <path>, quit\n";
	std::string line = {};
	for (;;) {
		std::cout << "rwz> " << std::flush;
		if (!std::getline(std::cin, line)) {
			break;
		}
		std::istringstream in(line);
		std::string cmd = {};
		std::string arg = {};
		in >> cmd >> arg;
		if (cmd.empty()) {
			continue;
		}
		if (cmd == "quit" || cmd == "exit") {
			break;
		}
		std::error_code ec = {};
		if (cmd == "ls") {
			list(arg.empty() ? root : in_view(arg, root));
		} else if (cmd == "cat") {
			cat(in_view(arg, root));
		} else if (cmd == "stat") {
			const auto p = in_view(arg, root);
			if (!rwz::fs::exists(p, ec)) {
				std::cout << "not found\n";
			} else {
				std::cout << (rwz::fs::is_directory(p, ec)
				                      ? "directory"
				                      : "file")
				          << "\n";
			}
		} else if (cmd == "put") {
			std::string text = {};
			std::getline(in >> std::ws, text);
			std::FILE* fp = rwz::stdio::fopen(
			        in_view(arg, root).string().c_str(), "wb");
			if (fp == nullptr) {
				std::cout << "error: cannot write " << arg << "\n";
				continue;
			}
			rwz::stdio::fwrite((text + "\n").data(), 1, text.size() + 1, fp);
			rwz::stdio::fclose(fp);
		} else if (cmd == "rm") {
			std::cout << (rwz::fs::remove(in_view(arg, root), ec)
			                      ? "removed\n"
			                      : "error: " + ec.message() + "\n");
		} else if (cmd == "mkdir") {
			std::cout << (rwz::fs::create_directory(in_view(arg, root), ec)
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
	auto session = open_or_report(a.positional[0], a);
	if (!session) {
		return 1;
	}
	shell(session->root()); // leaving the scope unmounts
	return 0;
}

// ---- revise: report the overlay, then write the revision -----------------------

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

int run_revise(const args& a)
{
	if (a.positional.size() != 1) {
		return usage();
	}
	auto session = open_or_report(a.positional[0], a);
	if (!session) {
		return 1;
	}

	const rwz::overlay_changes c = session->changes();
	std::cout << "overlay: " << c.written << " file(s) written, " << c.deleted
	          << " deleted, " << c.new_dirs << " directory(ies) recreated\n";
	if (c.empty()) {
		std::cout << "nothing to revise: the overlay has no changes.\n";
		return 0;
	}
	rwz::create_options options = {};
	if (a.compression) {
		options.compression = *a.compression;
	}
	const auto revised = session->revise(options);
	if (!revised) {
		std::cerr << "error: revision failed: "
		          << revise_text(revised.error()) << "\n";
		return 1;
	}
	std::cout << "revised archive: " << revised->string() << "\n";
	return 0;
}

// ---- create: a new archive from a game directory -------------------------------

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
	case rwz::create_error::already_exists:
		return "the output file already exists; choose another name";
	}
	return "unknown error";
}

int run_create(const args& a)
{
	if (a.positional.size() != 2) {
		return usage();
	}
	fs::path name = a.positional[0];
	if (a.out_dir && !name.has_parent_path()) {
		name = *a.out_dir / name;
	}
	rwz::create_options options = {};
	if (a.compression) {
		options.compression = *a.compression;
	}
	const auto made = rwz::archive_directory(name, a.positional[1], options);
	if (!made) {
		std::cerr << "error: cannot create the archive: "
		          << create_text(made.error()) << "\n";
		return 1;
	}
	std::cout << "created " << made->string() << " (" << fs::file_size(*made)
	          << " bytes) from " << a.positional[1] << "\n";
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
