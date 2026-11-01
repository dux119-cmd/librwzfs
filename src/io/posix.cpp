#include "rwzfs/io/posix.hpp"

#if !defined(_WIN32)

#include <dirent.h>
#include <fcntl.h>
#include <glob.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <optional>
#include <string>
#include <system_error>

#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/fs/resolve.hpp"
#include "rwzfs/io/archive_io.hpp"

namespace rwzfs::io::posix {

namespace fs = rwzfs::fs;
namespace aio = rwzfs::io::archive_io;

namespace {

// Resolves a caller path (CWD-05). On failure sets errno (for example ELOOP) and returns nullopt.
std::optional<fs::path> resolved(const char* text, bool follow_final = true) {
  std::error_code ec;
  const auto p = follow_final ? fs::resolve(fs::path(text), ec)
                              : fs::resolve_no_follow(fs::path(text), ec);
  if (ec) {
    errno = ec.value();
    return std::nullopt;
  }
  return p;
}

}  // namespace

// Every path wrapper resolves its argument (CWD-05) first. Archive paths go to archive_io,
// everything else passes through to the host call.

int close(int fd) { return aio::is_fd(fd) ? aio::close(fd) : ::close(fd); }

ssize_t read(int fd, void* buf, std::size_t count) {
  return aio::is_fd(fd) ? aio::read(fd, buf, count) : ::read(fd, buf, count);
}

ssize_t write(int fd, const void* buf, std::size_t count) {
  if (aio::is_fd(fd)) {
    errno = EBADF;  // archive descriptors are read-only
    return -1;
  }
  return ::write(fd, buf, count);
}

int open(const char* path, int flags, mode_t mode) {
  const auto p = resolved(path);
  if (!p) return -1;
  if (const auto r = fs::mounts::find(*p)) return aio::open(*r, flags);
  return ::open(p->c_str(), flags, mode);
}

int stat(const char* path, struct stat* buf) {
  const auto p = resolved(path);
  if (!p) return -1;
  if (const auto r = fs::mounts::find(*p)) return aio::stat(*r, buf);
  return ::stat(p->c_str(), buf);
}

int ioctl(int fd, unsigned long request, void* arg) {
  if (aio::is_fd(fd)) {
    errno = ENOTTY;
    return -1;
  }
  return ::ioctl(fd, request, arg);
}

off_t lseek(int fd, off_t offset, int whence) {
  return aio::is_fd(fd) ? aio::seek(fd, offset, whence) : ::lseek(fd, offset, whence);
}

int unlink(const char* path) {
  const auto p = resolved(path, false);
  if (!p) return -1;
  if (const auto r = fs::mounts::find(*p)) return aio::reject_write(*r);
  return ::unlink(p->c_str());
}

void* mmap(void* addr, std::size_t length, int prot, int flags, int fd, off_t offset) {
  if (aio::is_fd(fd)) {
    errno = ENODEV;
    return MAP_FAILED;
  }
  return ::mmap(addr, length, prot, flags, fd, offset);
}

char* realpath(const char* path, char* out) {
  const auto p = resolved(path);
  if (!p) return nullptr;
  if (const auto r = fs::mounts::find(*p)) return aio::realpath(*r, out);
  return ::realpath(p->c_str(), out);
}

DIR* opendir(const char* path) {
  const auto p = resolved(path);
  if (!p) return nullptr;
  if (const auto r = fs::mounts::find(*p)) return aio::opendir(*r);
  return ::opendir(p->c_str());
}

struct dirent* readdir(DIR* dir) {
  return aio::is_dir(dir) ? aio::readdir(dir) : ::readdir(dir);
}

int closedir(DIR* dir) { return aio::is_dir(dir) ? aio::closedir(dir) : ::closedir(dir); }

int mkdir(const char* path, mode_t mode) {
  const auto p = resolved(path, false);
  if (!p) return -1;
  if (const auto r = fs::mounts::find(*p)) return aio::reject_write(*r);
  return ::mkdir(p->c_str(), mode);
}

int rmdir(const char* path) {
  const auto p = resolved(path, false);
  if (!p) return -1;
  if (const auto r = fs::mounts::find(*p)) return aio::reject_write(*r);
  return ::rmdir(p->c_str());
}

int truncate(const char* path, off_t length) {
  const auto p = resolved(path);
  if (!p) return -1;
  if (const auto r = fs::mounts::find(*p)) return aio::reject_write(*r);
  return ::truncate(p->c_str(), length);
}

int access(const char* path, int mode) {
  const auto p = resolved(path);
  if (!p) return -1;
  if (const auto r = fs::mounts::find(*p)) return aio::access(*r, mode);
  return ::access(p->c_str(), mode);
}

int fsync(int fd) { return aio::is_fd(fd) ? aio::fsync(fd) : ::fsync(fd); }

// POSIX-19 / CWD-04
char* getcwd(char* buf, std::size_t size) {
  const auto archive = fs::cwd::archive_cwd();
  if (!archive) {
    return ::getcwd(buf, size);
  }
  if (buf == nullptr) {
    errno = EINVAL;
    return nullptr;
  }
  const auto text = archive->string();
  if (text.size() + 1 > size) {
    errno = ERANGE;
    return nullptr;
  }
  std::memcpy(buf, text.c_str(), text.size() + 1);
  return buf;
}

// CWD-03
int chdir(const char* path) {
  std::error_code ec;
  fs::change_directory(fs::path(path), ec);
  if (ec) {
    errno = ec.value();
    return -1;
  }
  return 0;
}

// Archive globbing is not implemented, so archive patterns abort with ENOTSUP.
int glob(const char* pattern, int flags, int (*errfunc)(const char*, int), glob_t* out) {
  const auto p = resolved(pattern);
  if (!p) return GLOB_ABORTED;
  if (fs::mounts::is_archive_path(*p)) {
    errno = ENOTSUP;
    return GLOB_ABORTED;
  }
  return ::glob(p->c_str(), flags, errfunc, out);
}

}  // namespace rwzfs::io::posix

#endif  // !defined(_WIN32)
