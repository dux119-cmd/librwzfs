#pragma once

#if !defined(_WIN32)

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cstdint>

#include "rwzfs/fs/mount_table.hpp"

// POSIX operations on archive paths that have an overlay. Reads see the merged view; writes go
// to the overlay. Each function returns what its POSIX counterpart returns and sets errno.
namespace rwzfs::io::overlay_posix {

using route = rwzfs::fs::mounts::route;

[[nodiscard]] int open(const route& r, int flags, mode_t mode);
[[nodiscard]] int stat(const route& r, struct stat* buf);
[[nodiscard]] int access(const route& r, int mode);
[[nodiscard]] int unlink(const route& r);
[[nodiscard]] int mkdir(const route& r, mode_t mode);
[[nodiscard]] int rmdir(const route& r);
[[nodiscard]] int truncate(const route& r, off_t length);
[[nodiscard]] DIR* opendir(const route& r);
[[nodiscard]] char* realpath(const route& r, char* resolved);

}  // namespace rwzfs::io::overlay_posix

#endif  // !defined(_WIN32)
