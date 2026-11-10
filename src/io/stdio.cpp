#include "rwzfs/io/stdio.hpp"

#include <sys/types.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <string>

#include "rwzfs/archive/backend.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/fs/resolve.hpp"
#include "rwzfs/overlay/cow.hpp"

namespace rwzfs::io::stdio {

namespace fs = rwzfs::fs;
namespace cow = rwzfs::overlay::cow;
using rwzfs::fs::mounts::route;

namespace {

// Resolves a caller path (CWD-05). On failure sets errno (for example ELOOP) and returns nullopt.
std::optional<fs::path> resolved(const std::string& text, bool follow_final = true) {
  std::error_code ec;
  const auto p = follow_final ? fs::resolve(fs::path(text), ec)
                              : fs::resolve_no_follow(fs::path(text), ec);
  if (ec) {
    errno = ec.value();
    return std::nullopt;
  }
  return p;
}

// Archive streams keep their buffer here until fclose. fmemopen does not own the buffer.
std::mutex& stream_mutex() {
  static std::mutex m;
  return m;
}

std::map<std::FILE*, std::unique_ptr<char[]>>& archive_streams() {
  static std::map<std::FILE*, std::unique_ptr<char[]>> m;
  return m;
}

bool has_write_intent(const char* mode) {
  for (const char* c = mode; *c != '\0'; ++c) {
    if (*c == 'w' || *c == 'a' || *c == '+') return true;
  }
  return false;
}

// Reads the whole archive file into memory. Returns false with errno set on failure.
bool read_all(const route& r, rwzfs::archive::entry e, char* out) {
  std::size_t got = 0;
  while (got < e.size) {
    std::error_code ec;
    const auto n = r.backend->read(e.inode, out + got, e.size - got, got, ec);
    if (ec) {
      errno = ec.value();
      return false;
    }
    if (n == 0) break;
    got += n;
  }
  if (got != e.size) {
    errno = EIO;
    return false;
  }
  return true;
}

std::FILE* open_archive_stream(const route& r) {
  if (!r.backend) {
    errno = ENOTSUP;
    return nullptr;
  }
  const auto e = r.backend->lookup(r.rel);
  if (!e) {
    errno = ENOENT;
    return nullptr;
  }
  if (e->type == rwzfs::archive::kind::directory) {
    errno = EISDIR;
    return nullptr;
  }
  if (e->type != rwzfs::archive::kind::file) {
    errno = ENOTSUP;
    return nullptr;
  }
#if defined(_WIN32)
  // In-memory streams need fmemopen, which Windows does not have. See docs/overlay_design.md.
  errno = ENOTSUP;
  return nullptr;
#else
  if (e->size == 0) {
    return std::fopen("/dev/null", "rb");  // an empty stream, with no buffer to own
  }

  auto buffer = std::make_unique<char[]>(e->size);
  if (!read_all(r, *e, buffer.get())) {
    return nullptr;
  }
  std::FILE* stream = fmemopen(buffer.get(), e->size, "rb");
  if (stream == nullptr) {
    return nullptr;
  }
  const std::scoped_lock lock(stream_mutex());
  archive_streams().emplace(stream, std::move(buffer));
  return stream;
#endif
}

}  // namespace

// STDIO-01
std::size_t fread(void* buf, std::size_t size, std::size_t count, std::FILE* stream) {
  return std::fread(buf, size, count, stream);
}

// STDIO-02
namespace {

bool mode_has(const char* mode, char c) {
  for (const char* m = mode; *m != '\0'; ++m) {
    if (*m == c) return true;
  }
  return false;
}

// ---- lazy copy-up for write-opens of archive files ------------------------
//
// A write-open of an archive-only file (r+, a, +) must not copy the file up at open: the copy
// happens on the first write. A FILE* has no hook on writes, so the stream is built on
// fopencookie (glibc) or funopen (BSD, macOS), whose callbacks do the work. Windows has neither;
// there the copy happens at open.

#if defined(__GLIBC__) || defined(__APPLE__) || defined(__FreeBSD__)
#define RWZFS_LAZY_STDIO 1
#endif

struct lazy_state {
  cow::layer l;
  bool readable = false;
  bool writable = false;
  bool append = false;
  std::uint64_t offset = 0;
  std::uint64_t archive_size = 0;
  std::uint64_t inode = 0;
  std::FILE* host = nullptr;  // overlay copy, once the file has been copied up
};

std::error_code errc_of(int e) { return std::error_code(e, std::generic_category()); }

// Copies the file up (once) and opens the overlay copy, positioned at the stream's offset.
std::error_code materialize_lazy(lazy_state& st) {
  if (st.host != nullptr) return {};
  const auto path = cow::materialize(st.l);
  if (!path) return path.error();
  st.host = std::fopen(path->string().c_str(), "r+b");
  if (st.host == nullptr) return errc_of(errno);
  if (!st.append && std::fseek(st.host, static_cast<long>(st.offset), SEEK_SET) != 0) {
    return errc_of(errno);
  }
  return {};
}

// Repositions the stream. Returns the new offset, or nullopt with errno set.
std::optional<std::uint64_t> seek_lazy(lazy_state& st, long long offset, int whence) {
  long long base = 0;
  switch (whence) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = static_cast<long long>(st.offset); break;
    case SEEK_END:
      if (st.host != nullptr) {
        if (std::fseek(st.host, 0, SEEK_END) != 0) return std::nullopt;
        const long end = std::ftell(st.host);
        if (end < 0) return std::nullopt;
        base = end;
      } else {
        base = static_cast<long long>(st.archive_size);
      }
      break;
    default:
      errno = EINVAL;
      return std::nullopt;
  }
  const long long target = base + offset;
  if (target < 0) {
    errno = EINVAL;
    return std::nullopt;
  }
  st.offset = static_cast<std::uint64_t>(target);
  if (st.host != nullptr && std::fseek(st.host, static_cast<long>(target), SEEK_SET) != 0) {
    return std::nullopt;
  }
  return st.offset;
}

// Reads come from the archive until the file is copied up; then from the overlay copy.
long lazy_read_impl(lazy_state& st, char* buf, std::size_t n) {
  if (!st.readable) {
    errno = EBADF;
    return -1;
  }
  if (st.host != nullptr) {
    const auto got = std::fread(buf, 1, n, st.host);
    st.offset += got;
    return static_cast<long>(got);
  }
  if (st.offset >= st.archive_size) return 0;
  const auto want = std::min<std::uint64_t>(n, st.archive_size - st.offset);
  std::error_code ec;
  const auto got = st.l.backend->read(st.inode, buf, static_cast<std::size_t>(want), st.offset, ec);
  if (ec) {
    errno = ec.value();
    return -1;
  }
  st.offset += got;
  return static_cast<long>(got);
}

// The first write copies the file up. Returns the number of bytes written, or -1 with errno.
long lazy_write_impl(lazy_state& st, const char* buf, std::size_t n) {
  if (!st.writable) {
    errno = EBADF;
    return -1;
  }
  if (const auto err = materialize_lazy(st)) {
    errno = err.value();
    return -1;
  }
  if (st.append && std::fseek(st.host, 0, SEEK_END) != 0) return -1;
  const auto put = std::fwrite(buf, 1, n, st.host);
  const long pos = std::ftell(st.host);
  if (pos >= 0) st.offset = static_cast<std::uint64_t>(pos);
  if (put == 0 && n > 0) {
    errno = EIO;
    return -1;
  }
  return static_cast<long>(put);
}

void lazy_close_impl(lazy_state* st) {
  if (st->host != nullptr) std::fclose(st->host);
  delete st;
}

#if defined(__GLIBC__)
ssize_t cookie_read(void* c, char* buf, size_t n) {
  return lazy_read_impl(*static_cast<lazy_state*>(c), buf, n);
}
ssize_t cookie_write(void* c, const char* buf, size_t n) {
  return lazy_write_impl(*static_cast<lazy_state*>(c), buf, n);
}
int cookie_seek(void* c, off64_t* off, int whence) {
  const auto r = seek_lazy(*static_cast<lazy_state*>(c), static_cast<long long>(*off), whence);
  if (!r) return -1;
  *off = static_cast<off64_t>(*r);
  return 0;
}
int cookie_close(void* c) {
  lazy_close_impl(static_cast<lazy_state*>(c));
  return 0;
}
#elif defined(__APPLE__) || defined(__FreeBSD__)
int funopen_read(void* c, char* buf, int n) {
  return static_cast<int>(lazy_read_impl(*static_cast<lazy_state*>(c), buf, static_cast<std::size_t>(n)));
}
int funopen_write(void* c, const char* buf, int n) {
  return static_cast<int>(lazy_write_impl(*static_cast<lazy_state*>(c), buf, static_cast<std::size_t>(n)));
}
fpos_t funopen_seek(void* c, fpos_t off, int whence) {
  const auto r = seek_lazy(*static_cast<lazy_state*>(c), static_cast<long long>(off), whence);
  return r ? static_cast<fpos_t>(*r) : static_cast<fpos_t>(-1);
}
int funopen_close(void* c) {
  lazy_close_impl(static_cast<lazy_state*>(c));
  return 0;
}
#endif

// Opens a lazy stream for an archive-only file. Returns nullptr when the platform has no callback
// stream (the caller then copies up at open).
std::FILE* open_lazy(const cow::layer& l, const char* mode, const archive::entry& info) {
#if defined(RWZFS_LAZY_STDIO)
  auto* st = new lazy_state;
  st->l = l;
  st->readable = mode_has(mode, 'r') || mode_has(mode, '+');
  st->writable = mode_has(mode, 'w') || mode_has(mode, 'a') || mode_has(mode, '+');
  st->append = mode_has(mode, 'a');
  st->archive_size = info.size;
  st->inode = info.inode;
#if defined(__GLIBC__)
  cookie_io_functions_t fns{};
  fns.read = cookie_read;
  fns.write = cookie_write;
  fns.seek = cookie_seek;
  fns.close = cookie_close;
  std::FILE* f = fopencookie(st, mode, fns);
#else
  std::FILE* f = funopen(st, st->readable ? funopen_read : nullptr,
                         st->writable ? funopen_write : nullptr, funopen_seek, funopen_close);
#endif
  if (f == nullptr) {
    delete st;
  }
  return f;
#else
  (void)l;
  (void)mode;
  (void)info;
  return nullptr;
#endif
}

// Routes with an overlay: reads see the merged view; writes go to the overlay file.
std::FILE* open_overlay_stream(const route& r, const char* mode) {
  const auto l = cow::layer_of(r);
  if (!has_write_intent(mode)) {
    const auto n = cow::lookup(l);
    if (!n) {
      errno = ENOENT;
      return nullptr;
    }
    if (n->where == cow::origin::overlay) return std::fopen(n->host.string().c_str(), mode);
    return open_archive_stream(r);
  }
  const bool create = mode_has(mode, 'w') || mode_has(mode, 'a');
  const bool truncate = mode_has(mode, 'w');
  const auto n = cow::lookup(l);
  if (n && n->info.type == archive::kind::directory) {
    errno = EISDIR;
    return nullptr;
  }
  // Archive-only file, written without truncation: copy up on the first write, not now.
  if (n && n->where == cow::origin::archive && !truncate) {
    if (std::FILE* lazy = open_lazy(l, mode, n->info)) return lazy;
  }
  // Everything else changes the overlay now (create, truncate, or the file is already there).
  const auto host = cow::prepare_write(l, create, false, truncate);
  if (!host) {
    errno = host.error().value();
    return nullptr;
  }
  return std::fopen(host->string().c_str(), mode);
}

}  // namespace

std::FILE* fopen(const char* path, const char* mode) {
  const auto p = resolved(path);
  if (!p) return nullptr;
  if (const auto r = fs::mounts::find(*p)) {
    if (!r->overlay.empty()) return open_overlay_stream(*r, mode);
    if (has_write_intent(mode)) {
      errno = EROFS;
      return nullptr;
    }
    return open_archive_stream(*r);
  }
  return std::fopen(p->string().c_str(), mode);
}

// STDIO-10
std::size_t fwrite(const void* buf, std::size_t size, std::size_t count, std::FILE* stream) {
  return std::fwrite(buf, size, count, stream);
}

// STDIO-11
int fseek(std::FILE* stream, long offset, int whence) { return std::fseek(stream, offset, whence); }

// STDIO-03
long ftell(std::FILE* stream) { return std::ftell(stream); }

// STDIO-04
int fflush(std::FILE* stream) { return std::fflush(stream); }

#if !defined(_WIN32)
// STDIO-12
int fseeko(std::FILE* stream, off_t offset, int whence) { return ::fseeko(stream, offset, whence); }

// STDIO-13
off_t ftello(std::FILE* stream) { return ::ftello(stream); }
#endif

// STDIO-09
int fclose(std::FILE* stream) {
  const int rc = std::fclose(stream);
  const std::scoped_lock lock(stream_mutex());
  archive_streams().erase(stream);  // the buffer is released only after the stream is closed
  return rc;
}

// STDIO-05
int remove(const char* path) {
  const auto p = resolved(path, false);
  if (!p) return -1;
  if (const auto r = fs::mounts::find(*p)) {
    if (!r->overlay.empty()) {
      const auto n = cow::lookup(cow::layer_of(*r));
      const auto err = !n ? std::make_error_code(std::errc::no_such_file_or_directory)
                          : n->info.type == archive::kind::directory
                                ? cow::remove_dir(cow::layer_of(*r))
                                : cow::remove_file(cow::layer_of(*r));
      if (err) {
        errno = err.value();
        return -1;
      }
      return 0;
    }
    errno = r->backend ? EROFS : ENOTSUP;
    return -1;
  }
  return std::remove(p->string().c_str());
}

// STDIO-06
int rename(const char* from, const char* to) {
  const auto src = resolved(from, false);
  if (!src) return -1;
  const auto dst = resolved(to, false);
  if (!dst) return -1;
  const auto rs = fs::mounts::find(*src);
  const auto rd = fs::mounts::find(*dst);
  if (rs || rd) {
    if (!rs || !rd || rs->overlay.empty() || rd->overlay.empty()) {
      const auto& r = rs ? *rs : *rd;
      errno = r.backend ? EROFS : ENOTSUP;
      return -1;
    }
    const auto err = cow::rename_file(cow::layer_of(*rs), cow::layer_of(*rd));
    if (err) {
      errno = err.value();
      return -1;
    }
    return 0;
  }
  return std::rename(src->string().c_str(), dst->string().c_str());
}

// STDIO-07. A stream for an archive path is returned in the failed state.
// Not yet supported for archive files: this stream type cannot wrap an in-memory copy.
std::ifstream input_file_stream(const std::string& path) {
  const auto p = resolved(path);
  if (p && !fs::mounts::is_archive_path(*p)) {
    return std::ifstream(*p);
  }
  std::ifstream failed;
  failed.setstate(std::ios::failbit);
  return failed;
}

// STDIO-08. A stream for an archive path is returned in the failed state.
std::ofstream output_file_stream(const std::string& path) {
  const auto p = resolved(path);
  if (p && !fs::mounts::is_archive_path(*p)) {
    return std::ofstream(*p);
  }
  std::ofstream failed;
  failed.setstate(std::ios::failbit);
  return failed;
}

}  // namespace rwzfs::io::stdio
