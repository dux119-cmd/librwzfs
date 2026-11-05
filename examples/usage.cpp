// End-to-end example: create an archive, mount it with an overlay, write through the wrappers,
// and create a revision that includes the overlay's changes. See docs/USAGE.md.
//
// usage: rwzfs_example <work directory>
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "rwzfs/archive/create.hpp"
#include "rwzfs/fs/archive_path.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/fs/filesystem.hpp"
#include "rwzfs/io/posix.hpp"
#include "rwzfs/mount.hpp"
#include "rwzfs/revise.hpp"

namespace fs = std::filesystem;

namespace {

std::string read_via_wrappers(const std::string& path) {
  const int fd = rwzfs::io::posix::open(path.c_str(), O_RDONLY, 0);
  if (fd < 0) return "<missing>";
  char buf[256] = {};
  const auto n = rwzfs::io::posix::read(fd, buf, sizeof(buf));
  rwzfs::io::posix::close(fd);
  return std::string(buf, n < 0 ? 0 : static_cast<std::size_t>(n));
}

void fail(const std::string& what) {
  std::cerr << "example failed: " << what << "\n";
  std::exit(1);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <work directory>\n";
    return 2;
  }
  const fs::path work = argv[1];
  fs::remove_all(work);
  fs::create_directories(work / "source" / "DIR");
  std::ofstream(work / "source" / "DIR" / "hello.txt") << "hello from the archive\n";
  std::ofstream(work / "source" / "readme.txt") << "readme\n";

  // 1. Create an archive. The compression is set explicitly here, because the default needs a
  //    zstd that exposes wlog and strat.
  rwzfs::create_options create;
  create.compression = "zstd:level=12";
  const auto archive = work / "project.dwarfs";
  if (!rwzfs::create_archive(work / "source", archive, create)) fail("create_archive");
  std::cout << "created " << archive << "\n";

  // 2. Mount it at the archive root with an overlay under a writable directory.
  rwzfs::mount_options options;
  options.write_root = work / "writes";
  const auto root = rwzfs::fs::archive_root().string();
  if (rwzfs::mount_archive(archive, root, options) != rwzfs::MountError::None) fail("mount");
  std::cout << "mounted at " << root << "\n";
  std::cout << "read  DIR/hello.txt: " << read_via_wrappers(root + "/DIR/hello.txt");

  // 3. Write through the wrappers. Nothing changes in the archive file.
  const int fd = rwzfs::io::posix::open((root + "/DIR/new.txt").c_str(), O_WRONLY | O_CREAT, 0644);
  if (fd < 0) fail("create new.txt");
  rwzfs::io::posix::write(fd, "written through the overlay\n", 28);
  rwzfs::io::posix::close(fd);
  rwzfs::io::posix::unlink((root + "/readme.txt").c_str());  // hidden by a whiteout
  std::cout << "read  DIR/new.txt:   " << read_via_wrappers(root + "/DIR/new.txt");

  // 4. Revise: flatten archive + overlay into project-rev1.dwarfs and clear the overlay.
  const auto revised = rwzfs::revise_archive(root, create);
  if (!revised) fail("revise_archive");
  std::cout << "revision " << *revised << "\n";

  // 5. Mount the revision with a fresh overlay and check the merged content is there.
  rwzfs::mount_options fresh;
  fresh.write_root = work / "writes-rev1";
  if (rwzfs::mount_archive(*revised, root, fresh) != rwzfs::MountError::None) fail("mount revision");
  const auto added = read_via_wrappers(root + "/DIR/new.txt");
  const auto removed = read_via_wrappers(root + "/readme.txt");
  std::cout << "rev1 DIR/new.txt:    " << added;
  std::cout << "rev1 readme.txt:     " << removed << "\n";
  if (added != "written through the overlay\n" || removed != "<missing>") fail("revision content");

  rwzfs::unmount_archive(root);
  std::cout << "ok\n";
  return 0;
}
