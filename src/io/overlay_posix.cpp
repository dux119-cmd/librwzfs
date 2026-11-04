#include "rwzfs/io/overlay_posix.hpp"

#if !defined(_WIN32)

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <system_error>

#include "rwzfs/io/archive_io.hpp"
#include "rwzfs/overlay/cow.hpp"

namespace rwzfs::io::overlay_posix {

namespace cow = rwzfs::overlay::cow;
namespace aio = rwzfs::io::archive_io;

namespace {

int fail(const std::error_code& ec) {
  errno = ec.value();
  return -1;
}

int fail(int err) {
  errno = err;
  return -1;
}

}  // namespace

namespace {

struct write_fd {
  cow::layer l;
  int flags{0};
  off_t offset{0};
  int host{-1};  // -1 until the file is copied up
};

std::mutex& table_mutex() {
  static std::mutex m;
  return m;
}

std::map<int, write_fd>& table() {
  static std::map<int, write_fd> t;
  return t;
}

bool readable(int flags) { return (flags & O_ACCMODE) != O_WRONLY; }
bool writable(int flags) { return (flags & O_ACCMODE) != O_RDONLY; }

// Copies the file up and opens the overlay copy. Idempotent once the host descriptor exists.
int ensure_host(write_fd& f) {
  if (f.host >= 0) return 0;
  const auto path = cow::materialize(f.l);
  if (!path) return fail(path.error());
  f.host = ::open(path->c_str(), O_RDWR | (f.flags & O_APPEND), 0);
  if (f.host < 0) return -1;
  if ((f.flags & O_APPEND) == 0 && ::lseek(f.host, f.offset, SEEK_SET) < 0) return -1;
  return 0;
}

// Before a deferred descriptor reads or seeks, switch to the host copy if another descriptor
// copied the file up in the meantime.
void sync_with_overlay(write_fd& f) {
  if (f.host >= 0) return;
  const auto n = cow::lookup(f.l);
  if (n && n->where == cow::origin::overlay) ensure_host(f);
}

}  // namespace

int open(const route& r, int flags, mode_t mode) {
  const auto l = cow::layer_of(r);
  constexpr int kWriteFlags = O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND;
  if ((flags & kWriteFlags) == 0) {
    const auto n = cow::lookup(l);
    if (!n) return fail(ENOENT);
    if (n->where == cow::origin::overlay) return ::open(n->host.c_str(), flags, mode);
    return aio::open(r, flags);  // archive entry, visible through the overlay
  }
  const bool create = (flags & O_CREAT) != 0;
  const bool exclusive = (flags & O_EXCL) != 0;
  const bool truncate = (flags & O_TRUNC) != 0;
  const auto n = cow::lookup(l);
  // Only a write-open of an archive-only file, without truncation, is deferred. Everything else
  // changes the overlay now: creating, truncating, or writing a file already in the overlay.
  const bool deferred = n && n->where == cow::origin::archive && !truncate && !exclusive;
  if (deferred) {
    if (n->info.type == rwzfs::archive::kind::directory) return fail(EISDIR);
    const int fd = aio::allocate_fd();
    const std::scoped_lock lock(table_mutex());
    table().emplace(fd, write_fd{l, flags, 0, -1});
    return fd;
  }
  const auto host = cow::prepare_write(l, create, exclusive, truncate);
  if (!host) return fail(host.error());
  // The file exists now; O_EXCL was checked by prepare_write and must not fail the host open.
  return ::open(host->c_str(), flags & ~O_EXCL, mode);
}

bool owns(int fd) {
  const std::scoped_lock lock(table_mutex());
  return table().contains(fd);
}

ssize_t read(int fd, void* buf, std::size_t count) {
  const std::scoped_lock lock(table_mutex());
  auto it = table().find(fd);
  if (it == table().end()) return fail(EBADF);
  auto& f = it->second;
  if (!readable(f.flags)) return fail(EBADF);
  sync_with_overlay(f);
  if (f.host >= 0) return ::read(f.host, buf, count);
  const auto n = cow::lookup(f.l);
  if (!n) return fail(ENOENT);
  std::error_code ec;
  const auto got = f.l.backend->read(n->info.inode, static_cast<char*>(buf), count,
                                     static_cast<std::uint64_t>(f.offset), ec);
  if (ec) return fail(ec.value());
  f.offset += static_cast<off_t>(got);
  return static_cast<ssize_t>(got);
}

ssize_t write(int fd, const void* buf, std::size_t count) {
  const std::scoped_lock lock(table_mutex());
  auto it = table().find(fd);
  if (it == table().end()) return fail(EBADF);
  auto& f = it->second;
  if (!writable(f.flags)) return fail(EBADF);
  if (ensure_host(f) != 0) return -1;  // the first real write: copy up now
  return ::write(f.host, buf, count);
}

off_t seek(int fd, off_t offset, int whence) {
  const std::scoped_lock lock(table_mutex());
  auto it = table().find(fd);
  if (it == table().end()) return fail(EBADF);
  auto& f = it->second;
  sync_with_overlay(f);
  if (f.host >= 0) return ::lseek(f.host, offset, whence);
  const auto n = cow::lookup(f.l);
  if (!n) return fail(ENOENT);
  off_t base = 0;
  switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = f.offset; break;
    case SEEK_END: base = static_cast<off_t>(n->info.size); break;
    default: return fail(EINVAL);
  }
  if (base + offset < 0) return fail(EINVAL);
  f.offset = base + offset;
  return f.offset;
}

int close(int fd) {
  const std::scoped_lock lock(table_mutex());
  auto it = table().find(fd);
  if (it == table().end()) return fail(EBADF);
  if (it->second.host >= 0) ::close(it->second.host);
  table().erase(it);
  return 0;
}

int fsync(int fd) {
  const std::scoped_lock lock(table_mutex());
  auto it = table().find(fd);
  if (it == table().end()) return fail(EBADF);
  return it->second.host >= 0 ? ::fsync(it->second.host) : 0;
}

int chmod(const route& r, mode_t mode) {
  const auto err = cow::set_mode(cow::layer_of(r), static_cast<std::uint32_t>(mode & 07777));
  return err ? fail(err) : 0;
}

int set_mtime(const route& r, std::int64_t seconds) {
  const auto err = cow::set_mtime(cow::layer_of(r), seconds);
  return err ? fail(err) : 0;
}

int stat(const route& r, struct stat* buf) {
  const auto n = cow::lookup(cow::layer_of(r));
  if (!n) return fail(ENOENT);
  aio::fill_stat(n->info, buf);
  return 0;
}

int access(const route& r, int mode) {
  (void)mode;  // overlay permissions follow the archive's; writes are allowed
  if (!cow::lookup(cow::layer_of(r))) return fail(ENOENT);
  return 0;
}

int unlink(const route& r) {
  const auto err = cow::remove_file(cow::layer_of(r));
  return err ? fail(err) : 0;
}

int mkdir(const route& r, mode_t mode) {
  (void)mode;  // directory permissions are not stored yet
  const auto err = cow::create_dir(cow::layer_of(r));
  return err ? fail(err) : 0;
}

int rmdir(const route& r) {
  const auto err = cow::remove_dir(cow::layer_of(r));
  return err ? fail(err) : 0;
}

int truncate(const route& r, off_t length) {
  if (length < 0) return fail(EINVAL);
  const auto err = cow::truncate(cow::layer_of(r), static_cast<std::uintmax_t>(length));
  return err ? fail(err) : 0;
}

DIR* opendir(const route& r) {
  const auto l = cow::layer_of(r);
  const auto n = cow::lookup(l);
  if (!n) {
    errno = ENOENT;
    return nullptr;
  }
  if (n->info.type != rwzfs::archive::kind::directory) {
    errno = ENOTDIR;
    return nullptr;
  }
  const auto kids = cow::list(l);
  if (!kids) {
    errno = ENOTDIR;
    return nullptr;
  }
  return aio::register_listing(n->info.inode, *kids);
}

char* realpath(const route& r, char* resolved) {
  if (!cow::lookup(cow::layer_of(r))) {
    errno = ENOENT;
    return nullptr;
  }
  const auto text = r.absolute.string();
  if (resolved == nullptr) return strdup(text.c_str());
  if (text.size() >= PATH_MAX) {
    errno = ENAMETOOLONG;
    return nullptr;
  }
  std::memcpy(resolved, text.c_str(), text.size() + 1);
  return resolved;
}

}  // namespace rwzfs::io::overlay_posix

#endif  // !defined(_WIN32)
