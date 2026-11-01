#include "rwzfs/fs/filesystem.hpp"

#include <cstdint>
#include <optional>
#include <system_error>

#include "rwzfs/archive/backend.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"

namespace rwzfs::fs {

namespace {

namespace stdfs = std::filesystem;
using mounts::route;

// Every wrapper resolves its paths first (CWD-05). Archive paths are answered from the
// backend where that is possible. Otherwise they fail: EROFS for mutations on a mounted
// archive, not_supported for queries that need a feature the archive does not have yet.

std::optional<route> archive_route(const path& p) { return mounts::find(p); }

bool routed(const path& p) { return archive_route(p).has_value(); }

void unsupported(std::error_code& ec) { ec = std::make_error_code(std::errc::not_supported); }

void refuse_write(const route& r, std::error_code& ec) {
  ec = std::make_error_code(r.backend ? std::errc::read_only_file_system
                                      : std::errc::not_supported);
}

void no_such_file(std::error_code& ec) {
  ec = std::make_error_code(std::errc::no_such_file_or_directory);
}

stdfs::file_type file_type_of(rwzfs::archive::kind k) {
  switch (k) {
    case rwzfs::archive::kind::file: return stdfs::file_type::regular;
    case rwzfs::archive::kind::directory: return stdfs::file_type::directory;
    case rwzfs::archive::kind::symlink: return stdfs::file_type::symlink;
    case rwzfs::archive::kind::other: break;
  }
  return stdfs::file_type::unknown;
}

}  // namespace

bool exists(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return false; }
    ec.clear();
    return r->backend->lookup(r->rel).has_value();
  }
  return stdfs::exists(p, ec);
}

stdfs::file_status status(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return {}; }
    const auto e = r->backend->lookup(r->rel);
    if (!e) {
      no_such_file(ec);
      return stdfs::file_status(stdfs::file_type::not_found);
    }
    ec.clear();
    return stdfs::file_status(file_type_of(e->type), static_cast<stdfs::perms>(e->mode & 07777));
  }
  return stdfs::status(p, ec);
}

void copy(const path& from, const path& to, stdfs::copy_options options, std::error_code& ec) {
  const auto src = resolve(from);
  const auto dst = resolve(to);
  if (const auto d = archive_route(dst)) { refuse_write(*d, ec); return; }
  if (routed(src)) { unsupported(ec); return; }
  stdfs::copy(src, dst, options, ec);
}

bool create_directories(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (const auto r = archive_route(p)) { refuse_write(*r, ec); return false; }
  return stdfs::create_directories(p, ec);
}

bool is_regular_file(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return false; }
    const auto e = r->backend->lookup(r->rel);
    if (!e) { no_such_file(ec); return false; }
    ec.clear();
    return e->type == rwzfs::archive::kind::file;
  }
  return stdfs::is_regular_file(p, ec);
}

bool is_directory(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return false; }
    const auto e = r->backend->lookup(r->rel);
    if (!e) { no_such_file(ec); return false; }
    ec.clear();
    return e->type == rwzfs::archive::kind::directory;
  }
  return stdfs::is_directory(p, ec);
}

// Lexical for archive paths: the archive has no symlinks to follow.
path absolute(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { ec.clear(); return p.lexically_normal(); }
  return stdfs::absolute(p, ec);
}

path canonical(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return {}; }
    if (!r->backend->lookup(r->rel)) { no_such_file(ec); return {}; }
    ec.clear();
    return r->absolute;  // no symlinks inside an archive, so this is already canonical
  }
  return stdfs::canonical(p, ec);
}

stdfs::directory_iterator directory_iterator(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { unsupported(ec); return {}; }
  return stdfs::directory_iterator(p, ec);
}

bool is_empty(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return false; }
    const auto e = r->backend->lookup(r->rel);
    if (!e) { no_such_file(ec); return false; }
    ec.clear();
    switch (e->type) {
      case rwzfs::archive::kind::directory: {
        const auto children = r->backend->list(r->rel);
        return children && children->empty();
      }
      case rwzfs::archive::kind::file: return e->size == 0;
      case rwzfs::archive::kind::symlink:
      case rwzfs::archive::kind::other: break;
    }
    unsupported(ec);
    return false;
  }
  return stdfs::is_empty(p, ec);
}

void permissions(const path& in, stdfs::perms perms, std::error_code& ec) {
  const auto p = resolve(in);
  if (const auto r = archive_route(p)) { refuse_write(*r, ec); return; }
  stdfs::permissions(p, perms, ec);
}

bool is_symlink(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return false; }
    const auto e = r->backend->lookup(r->rel);
    if (!e) { no_such_file(ec); return false; }
    ec.clear();
    return e->type == rwzfs::archive::kind::symlink;
  }
  return stdfs::is_symlink(p, ec);
}

bool equivalent(const path& a, const path& b, std::error_code& ec) {
  const auto pa = resolve(a);
  const auto pb = resolve(b);
  const auto ra = archive_route(pa);
  const auto rb = archive_route(pb);
  if (ra || rb) {
    if ((ra && !ra->backend) || (rb && !rb->backend)) { unsupported(ec); return false; }
    if (!ra || !rb) {
      // One archive path and one host path: they can never name the same file.
      ec.clear();
      return false;
    }
    const auto ea = ra->backend->lookup(ra->rel);
    const auto eb = rb->backend->lookup(rb->rel);
    if (!ea || !eb) { no_such_file(ec); return false; }
    ec.clear();
    return ra->backend == rb->backend && ea->inode == eb->inode;
  }
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
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return {}; }
    const auto e = r->backend->lookup(r->rel);
    if (!e) { no_such_file(ec); return {}; }
    if (e->type != rwzfs::archive::kind::symlink) {
      ec = std::make_error_code(std::errc::invalid_argument);
      return {};
    }
    unsupported(ec);  // link targets are not readable yet
    return {};
  }
  return stdfs::read_symlink(p, ec);
}

bool create_directory(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (const auto r = archive_route(p)) { refuse_write(*r, ec); return false; }
  return stdfs::create_directory(p, ec);
}

stdfs::space_info space(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { unsupported(ec); return {}; }
  return stdfs::space(p, ec);
}

stdfs::recursive_directory_iterator recursive_directory_iterator(const path& in,
                                                                 std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { unsupported(ec); return {}; }
  return stdfs::recursive_directory_iterator(p, ec);
}

path proximate(const path& in, const path& base, std::error_code& ec) {
  const auto p = resolve(in);
  const auto b = resolve(base);
  if (routed(p) || routed(b)) { unsupported(ec); return {}; }
  return stdfs::proximate(p, b, ec);
}

stdfs::file_time_type last_write_time(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (routed(p)) { unsupported(ec); return {}; }  // archive entries carry no timestamps yet
  return stdfs::last_write_time(p, ec);
}

std::uintmax_t remove_all(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (const auto r = archive_route(p)) { refuse_write(*r, ec); return 0; }
  return stdfs::remove_all(p, ec);
}

void resize_file(const path& in, std::uintmax_t size, std::error_code& ec) {
  const auto p = resolve(in);
  if (const auto r = archive_route(p)) { refuse_write(*r, ec); return; }
  stdfs::resize_file(p, size, ec);
}

bool remove(const path& in, std::error_code& ec) {
  const auto p = resolve(in);
  if (const auto r = archive_route(p)) { refuse_write(*r, ec); return false; }
  return stdfs::remove(p, ec);
}

void rename(const path& from, const path& to, std::error_code& ec) {
  const auto src = resolve(from);
  const auto dst = resolve(to);
  if (const auto r = archive_route(src)) { refuse_write(*r, ec); return; }
  if (const auto r = archive_route(dst)) { refuse_write(*r, ec); return; }
  stdfs::rename(src, dst, ec);
}

}  // namespace rwzfs::fs
