// Builds the test archive used by the dwarfs integration tests. Run as a CTest
// fixture setup step.
//
// usage: rwzfs_make_fixture <output.dwarfs> <scratch dir>
//
// Fixed content (any other archive fails the exact-content tests):
//   DIR/FILE.DAT            "payload"
//   DIR/SUB/inner.txt       "x"
//   DIR/UP        -> ../DIR/FILE.DAT    relative link inside a directory
//   DIR/UPROOT    -> ../../hostfile     relative link that climbs out of the
//   archive LINK_REL      -> DIR/FILE.DAT       relative link at the root
//   LINK_ABS
//   -> /rwz/DIR/FILE.DAT  absolute archive link HOSTLINK      ->
//   /etc/rwz-test-host absolute host link (target need not exist) DIRLINK ->
//   DIR                directory link NESTLINK      -> DIR/SUB directory link
//   to a nested directory DANGLING      -> NOPE               dangling link
//   LOOP_A
//   -> LOOP_B, LOOP_B -> LOOP_A  loop
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>

#include "rwzfs/archive/create.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
	if (argc != 3) {
		std::cerr << "usage: " << argv[0]
		          << " <output.dwarfs> <scratch dir>\n";
		return 2;
	}
	const fs::path out = argv[1];
	const fs::path src = argv[2];

	std::error_code ec;
	fs::create_directories(out.parent_path(), ec);
	fs::remove(out, ec);
	fs::remove_all(src, ec);
	fs::create_directories(src / "DIR" / "SUB");

	std::ofstream(src / "DIR" / "FILE.DAT") << "payload";
	std::ofstream(src / "DIR" / "SUB" / "inner.txt") << "x";

	fs::create_symlink("../DIR/FILE.DAT", src / "DIR" / "UP");
	fs::create_symlink("../../hostfile", src / "DIR" / "UPROOT");
	fs::create_symlink("DIR/FILE.DAT", src / "LINK_REL");
	fs::create_symlink("/rwz/DIR/FILE.DAT", src / "LINK_ABS");
	fs::create_symlink("/etc/rwz-test-host", src / "HOSTLINK");
	fs::create_symlink("DIR", src / "DIRLINK");
	fs::create_symlink("DIR/SUB", src / "NESTLINK");
	fs::create_symlink("NOPE", src / "DANGLING");
	fs::create_symlink("LOOP_B", src / "LOOP_A");
	fs::create_symlink("LOOP_A", src / "LOOP_B");

	rwz::create_options options;
	options.compression = "zstd:level=12"; // level-only: see docs/USAGE.md
	const auto made     = rwz::create_archive(src, out, options);
	if (!made) {
		std::cerr << "create_archive failed: error "
		          << static_cast<int>(made.error()) << "\n";
		return 1;
	}
	return 0;
}
