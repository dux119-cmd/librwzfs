#include "rwzfs/fs/filesystem.hpp"

#include <cstdint>
#include <system_error>

#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"

namespace rwzfs::fs {

namespace {

namespace stdfs = std::filesystem;

// Every wrapper resolves its paths first (CWD-05). Archive lookups are not implemented
// yet, so an archive path reports not_supported instead of touching the host.
bool routed(const path& p) { return mounts::is_archive_path(p); }

void reject(std::error_code& ec) { ec = std::make_error_code(std::errc::not_supported); }

}  // namespace

bool exists(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return false; }
  return stdfs::exists(p, ec);
}

stdfs::file_status status(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return {}; }
  return stdfs::status(p, ec);
}

void copy(const path& from, const path& to, stdfs::copy_options options, std::error_code& ec) {
  const auto src = resolve(from);
  const auto dst = resolve(to);
  if (routed(src) || routed(dst)) { reject(ec); return; }
  stdfs::copy(src, dst, options, ec);
}

bool create_directories(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return false; }
  return stdfs::create_directories(p, ec);
}

bool is_regular_file(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return false; }
  return stdfs::is_regular_file(p, ec);
}

bool is_directory(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return false; }
  return stdfs::is_directory(p, ec);
}

// Lexical for archive paths, so it does not need archive lookups.
path absolute(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { ec.clear(); return p.lexically_normal(); }
  return stdfs::absolute(p, ec);
}

path canonical(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return {}; }
  return stdfs::canonical(p, ec);
}

stdfs::directory_iterator directory_iterator(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return {}; }
  return stdfs::directory_iterator(p, ec);
}

bool is_empty(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return false; }
  return stdfs::is_empty(p, ec);
}

void permissions(const path& in, stdfs::perms perms, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return; }
  stdfs::permissions(p, perms, ec);
}

bool is_symlink(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return false; }
  return stdfs::is_symlink(p, ec);
}

bool equivalent(const path& a, const path& b, std::error_code& ec) {
  const auto pa = resolve(a);
  const auto pb = resolve(b);
  if (routed(pa) || routed(pb)) { reject(ec); return false; }
  return stdfs::equivalent(pa, pb, ec);
}

// CWD-01
path current_path(std::error_code& ec) {
  if (const auto base = cwd::archive_cwd()) {
    ec.clear();
    return *base;
  }
  return stdfs::current_path(ec);
}

// CWD-02
void current_path(const path& p, std::error_code& ec) { change_directory(p, ec); }

path read_symlink(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return {}; }
  return stdfs::read_symlink(p, ec);
}

bool create_directory(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return false; }
  return stdfs::create_directory(p, ec);
}

stdfs::space_info space(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return {}; }
  return stdfs::space(p, ec);
}

stdfs::recursive_directory_iterator recursive_directory_iterator(const path& in,
                                                                 std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return {}; }
  return stdfs::recursive_directory_iterator(p, ec);
}

path proximate(const path& in, const path& base, std::error_code& ec) {
  const auto p = resolve(in);
  const auto b = resolve(base);
  if (routed(p) || routed(b)) { reject(ec); return {}; }
  return stdfs::proximate(p, b, ec);
}

stdfs::file_time_type last_write_time(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return {}; }
  return stdfs::last_write_time(p, ec);
}

std::uintmax_t remove_all(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return 0; }
  return stdfs::remove_all(p, ec);
}

void resize_file(const path& in, std::uintmax_t size, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return; }
  stdfs::resize_file(p, size, ec);
}

bool remove(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { reject(ec); return false; }
  return stdfs::remove(p, ec);
}

void rename(const path& from, const path& to, std::error_code& ec) {
  const auto src = resolve(from);
  const auto dst = resolve(to);
  if (routed(src) || routed(dst)) { reject(ec); return; }
  stdfs::rename(src, dst, ec);
}

}  // namespace rwzfs::fs
