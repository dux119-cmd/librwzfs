#pragma once

#if !defined(_WIN32)

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cstddef>
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
[[nodiscard]] int chmod(const route& r, mode_t mode);
[[nodiscard]] int set_mtime(const route& r, std::int64_t seconds);

// Deferred-write descriptors. A write-open of an archive-only file does not copy it up; the copy
// happens on the first write. Until then reads come from the archive.
[[nodiscard]] bool owns(int fd);
[[nodiscard]] ssize_t read(int fd, void* buf, std::size_t count);
[[nodiscard]] ssize_t write(int fd, const void* buf, std::size_t count);
[[nodiscard]] off_t seek(int fd, off_t offset, int whence);
[[nodiscard]] int close(int fd);
[[nodiscard]] int fsync(int fd);

}  // namespace rwzfs::io::overlay_posix

#endif  // !defined(_WIN32)
