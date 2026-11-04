#include "rwzfs/io/stdio.hpp"

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

// Routes with an overlay: reads see the merged view, writes go to the overlay file.
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
  const auto host = cow::prepare_write(l, mode_has(mode, 'w') || mode_has(mode, 'a'), false,
                                       mode_has(mode, 'w'));
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

// STDIO-03
long ftell(std::FILE* stream) { return std::ftell(stream); }

// STDIO-04
int fflush(std::FILE* stream) { return std::fflush(stream); }

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
