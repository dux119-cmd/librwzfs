#pragma once

#if !defined(_WIN32)

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rwzfs/archive/backend.hpp"

#include "rwzfs/fs/mount_table.hpp"

namespace rwzfs::io::archive_io {

// Archive descriptors and DIR handles are numbered from this base, so they cannot collide
// with host descriptors in practice (host descriptors are far below it).
inline constexpr int kFdBase = 0x40000000;

using route = rwzfs::fs::mounts::route;

[[nodiscard]] bool is_fd(int fd);

// Next free descriptor number in the archive range. Shared with deferred-write descriptors.
[[nodiscard]] int allocate_fd();
[[nodiscard]] bool is_dir(DIR* dir);

// Each function reports ENOTSUP for a route without a backend.
[[nodiscard]] int stat(const route& r, struct stat* buf);
[[nodiscard]] int access(const route& r, int mode);
[[nodiscard]] int open(const route& r, int flags);
[[nodiscard]] ssize_t read(int fd, void* buf, std::size_t count);
[[nodiscard]] off_t seek(int fd, off_t offset, int whence);
[[nodiscard]] int close(int fd);
[[nodiscard]] int fsync(int fd);
[[nodiscard]] char* realpath(const route& r, char* resolved);
[[nodiscard]] DIR* opendir(const route& r);
[[nodiscard]] struct dirent* readdir(DIR* dir);
[[nodiscard]] int closedir(DIR* dir);

// Mutating calls on an archive path without an overlay: EROFS.
[[nodiscard]] int reject_write(const route& r);

// Fills a struct stat from an entry (type bits included in entry.mode).
void fill_stat(const rwzfs::archive::entry& e, struct stat* buf);

// Creates a DIR handle over a listing, including "." and "..". Used for merged overlay listings.
[[nodiscard]] DIR* register_listing(std::uint64_t self_inode,
                                    const std::vector<rwzfs::archive::child>& children);

}  // namespace rwzfs::io::archive_io

#endif  // !defined(_WIN32)
