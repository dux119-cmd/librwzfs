#include "rwzfs/io/posix.hpp"

#if !defined(_WIN32)

#include <cerrno>
#include <cstring>
#include <system_error>

#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"

namespace rwzfs::io::posix {

namespace fs = rwzfs::fs;

namespace {

// Archive file access is not implemented yet. Opening an archive path fails with ENOTSUP
// rather than reaching the host.
bool rejects_archive(const fs::path& p) {
  if (fs::mounts::is_archive_path(p)) {
    errno = ENOTSUP;
    return true;
  }
  return false;
}

}  // namespace

// Descriptors are host-only until archive files can be opened, so these pass through.

int close(int fd) { return ::close(fd); }

ssize_t read(int fd, void* buf, std::size_t count) { return ::read(fd, buf, count); }

ssize_t write(int fd, const void* buf, std::size_t count) { return ::write(fd, buf, count); }

int open(const char* path, int flags, mode_t mode) {
  const auto resolved = fs::resolve(fs::path(path));
  if (rejects_archive(resolved)) {
    return -1;
  }
  return ::open(resolved.c_str(), flags, mode);
}

off_t lseek(int fd, off_t offset, int whence) { return ::lseek(fd, offset, whence); }

int fsync(int fd) { return ::fsync(fd); }

// CWD-04
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

}  // namespace rwzfs::io::posix

#endif  // !defined(_WIN32)
