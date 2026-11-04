#include "rwzfs/io/archive_io.hpp"

#if !defined(_WIN32)

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace rwzfs::io::archive_io {

namespace {

using backend_ptr = std::shared_ptr<const rwzfs::archive::backend>;

struct open_file {
  backend_ptr backend;
  std::uint64_t inode{0};
  std::uint64_t size{0};
  std::uint64_t offset{0};
};

struct open_dir {
  std::vector<dirent> entries;
  std::size_t next{0};
};

std::mutex& state_mutex() {
  static std::mutex m;
  return m;
}

std::map<int, open_file>& files() {
  static std::map<int, open_file> m;
  return m;
}

int& next_fd() {
  static int n = kFdBase;
  return n;
}

std::map<const void*, std::unique_ptr<open_dir>>& dirs() {
  static std::map<const void*, std::unique_ptr<open_dir>> m;
  return m;
}

unsigned char d_type_of(rwzfs::archive::kind k) {
  switch (k) {
    case rwzfs::archive::kind::file: return DT_REG;
    case rwzfs::archive::kind::directory: return DT_DIR;
    case rwzfs::archive::kind::symlink: return DT_LNK;
    case rwzfs::archive::kind::other: break;
  }
  return DT_UNKNOWN;
}

int fail(int err) {
  errno = err;
  return -1;
}

}  // namespace

bool is_fd(int fd) { return fd >= kFdBase; }

int allocate_fd() {
  const std::scoped_lock lock(state_mutex());
  return next_fd()++;
}

bool is_dir(DIR* dir) {
  const std::scoped_lock lock(state_mutex());
  return dirs().contains(dir);
}

int reject_write(const route& r) { return fail(r.backend ? EROFS : ENOTSUP); }

void fill_stat(const rwzfs::archive::entry& e, struct stat* buf) {
  std::memset(buf, 0, sizeof(*buf));
  buf->st_mode = static_cast<mode_t>(e.mode);
  buf->st_ino = static_cast<ino_t>(e.inode);
  buf->st_size = static_cast<off_t>(e.size);
  buf->st_nlink = 1;
  buf->st_mtime = static_cast<time_t>(e.mtime);
}

int stat(const route& r, struct stat* buf) {
  if (!r.backend) return fail(ENOTSUP);
  const auto e = r.backend->lookup(r.rel);
  if (!e) return fail(ENOENT);
  fill_stat(*e, buf);
  return 0;
}

int access(const route& r, int mode) {
  if (!r.backend) return fail(ENOTSUP);
  if ((mode & W_OK) != 0) return fail(EROFS);
  if (!r.backend->lookup(r.rel)) return fail(ENOENT);
  return 0;
}

int open(const route& r, int flags) {
  if (!r.backend) return fail(ENOTSUP);
  constexpr int kWriteFlags = O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND;
  if ((flags & kWriteFlags) != 0) return fail(EROFS);
  const auto e = r.backend->lookup(r.rel);
  if (!e) return fail(ENOENT);
  if (e->type == rwzfs::archive::kind::directory) return fail(EISDIR);
  if (e->type != rwzfs::archive::kind::file) return fail(ENOTSUP);

  const std::scoped_lock lock(state_mutex());
  const int fd = next_fd()++;
  files().emplace(fd, open_file{r.backend, e->inode, e->size, 0});
  return fd;
}

ssize_t read(int fd, void* buf, std::size_t count) {
  open_file f;
  {
    const std::scoped_lock lock(state_mutex());
    const auto it = files().find(fd);
    if (it == files().end()) return fail(EBADF);
    f = it->second;
  }
  std::error_code ec;
  const auto got = f.backend->read(f.inode, static_cast<char*>(buf), count, f.offset, ec);
  if (ec) return fail(ec.value());
  {
    const std::scoped_lock lock(state_mutex());
    if (const auto it = files().find(fd); it != files().end()) {
      it->second.offset += got;
    }
  }
  return static_cast<ssize_t>(got);
}

off_t seek(int fd, off_t offset, int whence) {
  const std::scoped_lock lock(state_mutex());
  const auto it = files().find(fd);
  if (it == files().end()) return fail(EBADF);
  std::int64_t base = 0;
  switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = static_cast<std::int64_t>(it->second.offset); break;
    case SEEK_END: base = static_cast<std::int64_t>(it->second.size); break;
    default: return fail(EINVAL);
  }
  const std::int64_t target = base + static_cast<std::int64_t>(offset);
  if (target < 0) return fail(EINVAL);
  it->second.offset = static_cast<std::uint64_t>(target);
  return static_cast<off_t>(target);
}

int close(int fd) {
  const std::scoped_lock lock(state_mutex());
  return files().erase(fd) == 1 ? 0 : fail(EBADF);
}

int fsync(int fd) {
  const std::scoped_lock lock(state_mutex());
  return files().contains(fd) ? 0 : fail(EBADF);
}

char* realpath(const route& r, char* resolved) {
  if (!r.backend) {
    errno = ENOTSUP;
    return nullptr;
  }
  if (!r.backend->lookup(r.rel)) {
    errno = ENOENT;
    return nullptr;
  }
  const auto text = r.absolute.string();
  if (resolved == nullptr) {
    return strdup(text.c_str());
  }
  if (text.size() >= PATH_MAX) {
    errno = ENAMETOOLONG;
    return nullptr;
  }
  std::memcpy(resolved, text.c_str(), text.size() + 1);
  return resolved;
}

DIR* register_listing(std::uint64_t self_inode, const std::vector<rwzfs::archive::child>& children) {
  auto state = std::make_unique<open_dir>();
  for (const char* dot : {".", ".."}) {
    dirent d{};
    d.d_ino = static_cast<ino_t>(self_inode);
    d.d_type = DT_DIR;
    std::memcpy(d.d_name, dot, std::strlen(dot) + 1);
    state->entries.push_back(d);
  }
  for (const auto& c : children) {
    if (c.name.size() >= sizeof(dirent::d_name)) {
      errno = ENAMETOOLONG;
      return nullptr;
    }
    dirent d{};
    d.d_ino = static_cast<ino_t>(c.info.inode);
    d.d_type = d_type_of(c.info.type);
    std::memcpy(d.d_name, c.name.c_str(), c.name.size() + 1);
    state->entries.push_back(d);
  }
  const auto key = static_cast<const void*>(state.get());
  const std::scoped_lock lock(state_mutex());
  dirs().emplace(key, std::move(state));
  return reinterpret_cast<DIR*>(const_cast<void*>(key));
}

DIR* opendir(const route& r) {
  if (!r.backend) {
    errno = ENOTSUP;
    return nullptr;
  }
  const auto e = r.backend->lookup(r.rel);
  if (!e) {
    errno = ENOENT;
    return nullptr;
  }
  const auto children = r.backend->list(r.rel);
  if (!children) {
    errno = ENOTDIR;
    return nullptr;
  }

  auto state = std::make_unique<open_dir>();
  for (const char* dot : {".", ".."}) {
    dirent d{};
    d.d_ino = static_cast<ino_t>(e->inode);
    d.d_type = DT_DIR;
    std::memcpy(d.d_name, dot, std::strlen(dot) + 1);
    state->entries.push_back(d);
  }
  for (const auto& c : *children) {
    if (c.name.size() >= sizeof(dirent::d_name)) {
      errno = ENAMETOOLONG;
      return nullptr;
    }
    dirent d{};
    d.d_ino = static_cast<ino_t>(c.info.inode);
    d.d_type = d_type_of(c.info.type);
    std::memcpy(d.d_name, c.name.c_str(), c.name.size() + 1);
    state->entries.push_back(d);
  }

  // The state object's address is unique while it is live, so it doubles as the DIR handle.
  const auto key = static_cast<const void*>(state.get());
  const std::scoped_lock lock(state_mutex());
  dirs().emplace(key, std::move(state));
  return reinterpret_cast<DIR*>(const_cast<void*>(key));
}

struct dirent* readdir(DIR* dir) {
  const std::scoped_lock lock(state_mutex());
  const auto it = dirs().find(static_cast<const void*>(dir));
  if (it == dirs().end()) {
    errno = EBADF;
    return nullptr;
  }
  open_dir& d = *it->second;
  if (d.next >= d.entries.size()) {
    return nullptr;
  }
  return &d.entries[d.next++];
}

int closedir(DIR* dir) {
  const std::scoped_lock lock(state_mutex());
  return dirs().erase(static_cast<const void*>(dir)) == 1 ? 0 : fail(EBADF);
}

}  // namespace rwzfs::io::archive_io

#endif  // !defined(_WIN32)
