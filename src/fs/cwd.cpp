#include "rwzfs/fs/cwd.hpp"

#include <mutex>

#include "rwzfs/fs/mount_table.hpp"

namespace rwzfs::fs {

namespace {

namespace stdfs = std::filesystem;

std::mutex& cwd_mutex() {
  static std::mutex m;
  return m;
}

std::optional<path>& cwd_slot() {
  static std::optional<path> current;
  return current;
}

}  // namespace

path resolve(const path& p) {
  if (p.is_absolute()) {
    return p;
  }
  const auto base = cwd::archive_cwd();
  if (!base) {
    return p;
  }
  auto joined = (*base / p).lexically_normal();
  // lexically_normal() keeps a trailing separator for "dir/."; drop it, as the archive spec requires.
  if (!joined.has_filename() && joined.has_parent_path()) {
    joined = joined.parent_path();
  }
  return joined;
}

void change_directory(const path& p, std::error_code& ec) {
  const auto target = resolve(p);
  if (mounts::is_archive_path(target)) {
    ec = std::make_error_code(std::errc::not_supported);
    return;
  }
  stdfs::current_path(target, ec);
  if (!ec) {
    cwd::clear();
  }
}

namespace cwd {

std::optional<path> archive_cwd() {
  const std::scoped_lock lock(cwd_mutex());
  return cwd_slot();
}

bool set_archive_cwd(const path& p) {
  if (!p.is_absolute() || !mounts::is_archive_path(p)) {
    return false;
  }
  const std::scoped_lock lock(cwd_mutex());
  cwd_slot() = p.lexically_normal();
  return true;
}

void clear() {
  const std::scoped_lock lock(cwd_mutex());
  cwd_slot().reset();
}

}  // namespace cwd

}  // namespace rwzfs::fs
