#include "rwzfs/fs/cwd.hpp"

#include <mutex>

#include "rwzfs/archive/backend.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/fs/resolve.hpp"

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

void change_directory(const path& p, std::error_code& ec) {
  const auto target = resolve(p, ec);
  if (ec) return;
  if (const auto r = mounts::find(target)) {
    if (!r->backend) {
      ec = std::make_error_code(std::errc::not_supported);
      return;
    }
    const auto e = r->backend->lookup(r->rel);
    if (!e) {
      ec = std::make_error_code(std::errc::no_such_file_or_directory);
      return;
    }
    if (e->type != rwzfs::archive::kind::directory) {
      ec = std::make_error_code(std::errc::not_a_directory);
      return;
    }
    ec.clear();
    [[maybe_unused]] const bool entered = cwd::set_archive_cwd(target);
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
