#include "rwzfs/io/overlay_posix.hpp"

#if !defined(_WIN32)

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstring>
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
  const auto host = cow::prepare_write(l, create, exclusive, truncate);
  if (!host) return fail(host.error());
  // The file exists now; O_EXCL was checked by prepare_write and must not fail the host open.
  return ::open(host->c_str(), flags & ~O_EXCL, mode);
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
