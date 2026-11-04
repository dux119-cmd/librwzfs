#include "rwzfs/fs/filesystem.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "rwzfs/archive/backend.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/fs/resolve.hpp"
#include "rwzfs/overlay/cow.hpp"

namespace rwzfs::fs {

namespace {

namespace stdfs = std::filesystem;
using mounts::route;
namespace cow = rwzfs::overlay::cow;

// Archive paths are answered through the copy-on-write layer, which also serves routes without
// an overlay (read-only). Mutations need an overlay; without one they fail with EROFS.

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

void set_ec(std::error_code& ec, const std::error_code& err) {
  if (err) ec = err; else ec.clear();
}

stdfs::file_type file_type_of(archive::kind k) {
  switch (k) {
    case archive::kind::file: return stdfs::file_type::regular;
    case archive::kind::directory: return stdfs::file_type::directory;
    case archive::kind::symlink: return stdfs::file_type::symlink;
    case archive::kind::other: break;
  }
  return stdfs::file_type::unknown;
}

// Entry for an archive-routed path, or nullopt if it does not exist (also when hidden).
std::optional<archive::entry> entry_of(const route& r) {
  const auto n = cow::lookup(cow::layer_of(r));
  if (!n) return std::nullopt;
  return n->info;
}

bool has_overlay(const route& r) { return !r.overlay.empty(); }

// Removes a merged tree; returns the number of entries removed, or an error.
std::expected<std::uintmax_t, std::error_code> remove_tree(const cow::layer& l) {
  const auto n = cow::lookup(l);
  if (!n) return std::uintmax_t{0};
  std::uintmax_t count = 0;
  if (n->info.type == archive::kind::directory) {
    if (const auto kids = cow::list(l)) {
      for (const auto& c : *kids) {
        cow::layer child = l;
        child.rel = l.rel.empty() ? c.name : l.rel + "/" + c.name;
        const auto sub = remove_tree(child);
        if (!sub) return std::unexpected(sub.error());
        count += *sub;
      }
    }
    if (auto err = cow::remove_dir(l)) return std::unexpected(err);
  } else {
    if (auto err = cow::remove_file(l)) return std::unexpected(err);
  }
  return count + 1;
}

}  // namespace

bool exists(const path& in, std::error_code& ec) {
  const auto p = resolve(in, ec);
  if (ec) return false;
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return false; }
    ec.clear();
    return entry_of(*r).has_value();
  }
  return stdfs::exists(p, ec);
}

stdfs::file_status status(const path& in, std::error_code& ec) {
  const auto p = resolve(in, ec);
  if (ec) return {};
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return {}; }
    const auto e = entry_of(*r);
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
  const auto src = resolve(from, ec);
  if (ec) return;
  const auto dst = resolve_no_follow(to, ec);
  if (ec) return;
  if (const auto d = archive_route(dst)) { refuse_write(*d, ec); return; }
  if (routed(src)) { unsupported(ec); return; }
  stdfs::copy(src, dst, options, ec);
}

bool create_directories(const path& in, std::error_code& ec) {
  const auto p = resolve_no_follow(in, ec);
  if (ec) return false;
  const auto r = archive_route(p);
  if (!r) return stdfs::create_directories(p, ec);
  if (!has_overlay(*r)) { refuse_write(*r, ec); return false; }

  // Create each missing level of the merged view, outermost first.
  bool created = false;
  std::vector<std::string> levels;
  for (std::size_t pos = 0; pos <= r->rel.size(); ++pos) {
    if (pos == r->rel.size() || r->rel[pos] == '/') {
      if (pos > 0) levels.push_back(r->rel.substr(0, pos));
    }
  }
  for (const auto& rel : levels) {
    cow::layer l = cow::layer_of(*r);
    l.rel = rel;
    if (const auto n = cow::lookup(l)) {
      if (n->info.type != archive::kind::directory) {
        ec = std::make_error_code(std::errc::not_a_directory);
        return false;
      }
      continue;
    }
    set_ec(ec, cow::create_dir(l));
    if (ec) return false;
    created = true;
  }
  ec.clear();
  return created;
}

bool is_regular_file(const path& in, std::error_code& ec) {
  const auto p = resolve(in, ec);
  if (ec) return false;
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return false; }
    const auto e = entry_of(*r);
    if (!e) { no_such_file(ec); return false; }
    ec.clear();
    return e->type == archive::kind::file;
  }
  return stdfs::is_regular_file(p, ec);
}

bool is_directory(const path& in, std::error_code& ec) {
  const auto p = resolve(in, ec);
  if (ec) return false;
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return false; }
    const auto e = entry_of(*r);
    if (!e) { no_such_file(ec); return false; }
    ec.clear();
    return e->type == archive::kind::directory;
  }
  return stdfs::is_directory(p, ec);
}

// Lexical, as std::absolute is: it does not touch storage, so symlinks are not followed.
path absolute(const path& in, std::error_code& ec) {
  ec.clear();
  const auto p = resolve_lexical(in);
  if (routed(p)) return p;
  return stdfs::absolute(p, ec);
}

path canonical(const path& in, std::error_code& ec) {
  const auto p = resolve(in, ec);
  if (ec) return {};
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return {}; }
    if (!entry_of(*r)) { no_such_file(ec); return {}; }
    ec.clear();
    return r->absolute;  // resolve() already followed every archive symlink
  }
  return stdfs::canonical(p, ec);
}

bool is_empty(const path& in, std::error_code& ec) {
  const auto p = resolve(in, ec);
  if (ec) return false;
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return false; }
    const auto e = entry_of(*r);
    if (!e) { no_such_file(ec); return false; }
    ec.clear();
    switch (e->type) {
      case archive::kind::directory: {
        const auto children = cow::list(cow::layer_of(*r));
        return children && children->empty();
      }
      case archive::kind::file: return e->size == 0;
      case archive::kind::symlink:
      case archive::kind::other: break;
    }
    unsupported(ec);
    return false;
  }
  return stdfs::is_empty(p, ec);
}

void permissions(const path& in, stdfs::perms perms, std::error_code& ec) {
  const auto p = resolve(in, ec);
  if (ec) return;
  if (const auto r = archive_route(p)) {
    if (!has_overlay(*r)) { refuse_write(*r, ec); return; }
    set_ec(ec, cow::set_mode(cow::layer_of(*r), static_cast<std::uint32_t>(perms) & 07777));
    return;
  }
  stdfs::permissions(p, perms, ec);
}

bool is_symlink(const path& in, std::error_code& ec) {
  const auto p = resolve_no_follow(in, ec);
  if (ec) return false;
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return false; }
    const auto e = entry_of(*r);
    if (!e) { no_such_file(ec); return false; }
    ec.clear();
    return e->type == archive::kind::symlink;
  }
  return stdfs::is_symlink(p, ec);
}

bool equivalent(const path& a, const path& b, std::error_code& ec) {
  const auto pa = resolve(a, ec);
  if (ec) return false;
  const auto pb = resolve(b, ec);
  if (ec) return false;
  const auto ra = archive_route(pa);
  const auto rb = archive_route(pb);
  if (ra || rb) {
    if ((ra && !ra->backend) || (rb && !rb->backend)) { unsupported(ec); return false; }
    if (!ra || !rb) return false;  // one archive path and one host path: never the same file
    const auto na = cow::lookup(cow::layer_of(*ra));
    const auto nb = cow::lookup(cow::layer_of(*rb));
    if (!na || !nb) { no_such_file(ec); return false; }
    ec.clear();
    if (na->where == cow::origin::overlay && nb->where == cow::origin::overlay) {
      return stdfs::equivalent(na->host, nb->host, ec);
    }
    if (na->where == cow::origin::archive && nb->where == cow::origin::archive) {
      return ra->backend == rb->backend && na->info.inode == nb->info.inode;
    }
    return false;
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
  const auto p = resolve_no_follow(in, ec);
  if (ec) return {};
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return {}; }
    const auto n = cow::lookup(cow::layer_of(*r));
    if (!n) { no_such_file(ec); return {}; }
    if (n->info.type != archive::kind::symlink) {
      ec = std::make_error_code(std::errc::invalid_argument);
      return {};
    }
    const auto target = r->backend->link_target(r->rel);
    if (!target) { unsupported(ec); return {}; }
    return path(*target);
  }
  return stdfs::read_symlink(p, ec);
}

bool create_directory(const path& in, std::error_code& ec) {
  const auto p = resolve_no_follow(in, ec);
  if (ec) return false;
  if (const auto r = archive_route(p)) {
    if (!has_overlay(*r)) { refuse_write(*r, ec); return false; }
    set_ec(ec, cow::create_dir(cow::layer_of(*r)));
    return !ec;
  }
  return stdfs::create_directory(p, ec);
}

stdfs::space_info space(const path& in, std::error_code& ec) {
  const auto p = resolve(in, ec);
  if (ec) return {};
  if (routed(p)) { unsupported(ec); return {}; }
  return stdfs::space(p, ec);
}

path proximate(const path& in, const path& base, std::error_code& ec) {
  const auto p = resolve(in, ec);
  if (ec) return {};
  const auto b = resolve(base, ec);
  if (ec) return {};
  if (routed(p) || routed(b)) { unsupported(ec); return {}; }
  return stdfs::proximate(p, b, ec);
}

stdfs::file_time_type last_write_time(const path& in, std::error_code& ec) {
  const auto p = resolve(in, ec);
  if (ec) return {};
  if (const auto r = archive_route(p)) {
    if (!r->backend) { unsupported(ec); return {}; }
    const auto n = cow::lookup(cow::layer_of(*r));
    if (!n) { no_such_file(ec); return {}; }
    ec.clear();
    const auto sys = std::chrono::system_clock::time_point(std::chrono::seconds(n->info.mtime));
    return std::chrono::clock_cast<stdfs::file_time_type::clock>(sys);
  }
  return stdfs::last_write_time(p, ec);
}

void last_write_time(const path& in, stdfs::file_time_type t, std::error_code& ec) {
  const auto p = resolve(in, ec);
  if (ec) return;
  if (const auto r = archive_route(p)) {
    if (!has_overlay(*r)) { refuse_write(*r, ec); return; }
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
    set_ec(ec, cow::set_mtime(cow::layer_of(*r), static_cast<std::int64_t>(seconds)));
    return;
  }
  stdfs::last_write_time(p, t, ec);
}

std::uintmax_t remove_all(const path& in, std::error_code& ec) {
  const auto p = resolve_no_follow(in, ec);
  if (ec) return 0;
  if (const auto r = archive_route(p)) {
    if (!has_overlay(*r)) { refuse_write(*r, ec); return 0; }
    const auto removed = remove_tree(cow::layer_of(*r));
    if (!removed) { ec = removed.error(); return 0; }
    ec.clear();
    return *removed;
  }
  return stdfs::remove_all(p, ec);
}

void resize_file(const path& in, std::uintmax_t size, std::error_code& ec) {
  const auto p = resolve(in, ec);
  if (ec) return;
  if (const auto r = archive_route(p)) {
    if (!has_overlay(*r)) { refuse_write(*r, ec); return; }
    set_ec(ec, cow::truncate(cow::layer_of(*r), size));
    return;
  }
  stdfs::resize_file(p, size, ec);
}

bool remove(const path& in, std::error_code& ec) {
  const auto p = resolve_no_follow(in, ec);
  if (ec) return false;
  if (const auto r = archive_route(p)) {
    if (!has_overlay(*r)) { refuse_write(*r, ec); return false; }
    const auto n = cow::lookup(cow::layer_of(*r));
    if (!n) { ec.clear(); return false; }  // like std::filesystem::remove: absent is not an error
    set_ec(ec, n->info.type == archive::kind::directory
                   ? cow::remove_dir(cow::layer_of(*r))
                   : cow::remove_file(cow::layer_of(*r)));
    return !ec;
  }
  return stdfs::remove(p, ec);
}

void rename(const path& from, const path& to, std::error_code& ec) {
  const auto src = resolve_no_follow(from, ec);
  if (ec) return;
  const auto dst = resolve_no_follow(to, ec);
  if (ec) return;
  const auto rs = archive_route(src);
  const auto rd = archive_route(dst);
  if (rs || rd) {
    if (!rs || !rd || !has_overlay(*rs) || !has_overlay(*rd)) {
      refuse_write(rs ? *rs : *rd, ec);
      return;
    }
    set_ec(ec, cow::rename_file(cow::layer_of(*rs), cow::layer_of(*rd)));
    return;
  }
  stdfs::rename(src, dst, ec);
}

}  // namespace rwzfs::fs
