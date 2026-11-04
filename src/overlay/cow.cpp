#include "rwzfs/overlay/cow.hpp"
#include "rwzfs/overlay/host_meta.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <map>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace rwzfs::overlay::cow {

namespace stdfs = std::filesystem;

namespace {

constexpr std::uintmax_t kCopyChunk = 64 * 1024;

std::error_code ec_of(std::errc e) { return std::make_error_code(e); }

bool has_overlay(const layer& l) { return !l.overlay.empty(); }

// <overlay>/<area>/<rel>. Empty when there is no overlay, so nothing is ever read from the cwd.
stdfs::path area_path(const layer& l, std::string_view area, const std::string& rel) {
  if (!has_overlay(l)) return {};
  auto p = l.overlay / std::string(area);
  if (!rel.empty()) p /= stdfs::path(rel);
  return p;
}

stdfs::path data_of(const layer& l, const std::string& rel) { return area_path(l, "data", rel); }
stdfs::path whiteout_of(const layer& l, const std::string& rel) {
  return area_path(l, "whiteouts", rel);
}
stdfs::path opaque_of(const layer& l, const std::string& rel) {
  return area_path(l, "opaque", rel);
}

// Markers are regular files. A directory under the marker tree only holds child markers.
bool marker(const stdfs::path& p) {
  std::error_code ec;
  return stdfs::is_regular_file(p, ec);
}

std::string parent_of(const std::string& rel) {
  const auto pos = rel.rfind('/');
  return pos == std::string::npos ? std::string{} : rel.substr(0, pos);
}

// Strict ancestors of rel, outermost first: "a/b/c" -> {"a", "a/b"}.
std::vector<std::string> ancestors(const std::string& rel) {
  std::vector<std::string> out;
  for (std::size_t pos = rel.find('/'); pos != std::string::npos; pos = rel.find('/', pos + 1)) {
    out.push_back(rel.substr(0, pos));
  }
  return out;
}

std::optional<archive::entry> info_of_host(const stdfs::path& p) {
  return host_meta::entry_of(p);
}

// Archive entries under `rel` are visible unless a deletion or opaque marker hides them.
bool archive_visible(const layer& l) {
  if (marker(whiteout_of(l, l.rel))) return false;
  for (const auto& a : ancestors(l.rel)) {
    if (marker(whiteout_of(l, a)) || marker(opaque_of(l, a))) return false;
  }
  return true;
}

bool archive_has(const layer& l) {
  return archive_visible(l) && l.backend && l.backend->lookup(l.rel).has_value();
}

std::error_code make_marker(const stdfs::path& p) {
  std::error_code ec;
  stdfs::create_directories(p.parent_path(), ec);
  if (ec) return ec;
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  return out ? std::error_code{} : ec_of(std::errc::io_error);
}

void remove_marker(const stdfs::path& p) {
  std::error_code ec;
  stdfs::remove(p, ec);
}

// Writes the archive file's bytes to the overlay path.
std::error_code copy_up(const layer& l, const archive::entry& e) {
  const auto dst = data_of(l, l.rel);
  std::error_code ec;
  stdfs::create_directories(dst.parent_path(), ec);
  if (ec) return ec;
  std::ofstream out(dst, std::ios::binary | std::ios::trunc);
  if (!out) return ec_of(std::errc::io_error);
  std::vector<char> buf(static_cast<std::size_t>(std::min<std::uintmax_t>(kCopyChunk, e.size)));
  std::uint64_t offset = 0;
  while (offset < e.size) {
    const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(buf.size(), e.size - offset));
    const auto got = l.backend->read(e.inode, buf.data(), want, offset, ec);
    if (ec) return ec;
    if (got == 0) return ec_of(std::errc::io_error);
    out.write(buf.data(), static_cast<std::streamsize>(got));
    if (!out) return ec_of(std::errc::io_error);
    offset += got;
  }
  return {};
}

layer parent_layer(const layer& l) {
  layer p = l;
  p.rel = parent_of(l.rel);
  return p;
}

// Parent must exist and be a directory. Returns an error code otherwise.
std::error_code check_parent(const layer& l) {
  const auto info = lookup(parent_layer(l));
  if (!info) return ec_of(std::errc::no_such_file_or_directory);
  if (info->info.type != archive::kind::directory) return ec_of(std::errc::not_a_directory);
  return {};
}

std::error_code make_parent_dirs(const layer& l) {
  std::error_code ec;
  stdfs::create_directories(data_of(l, parent_of(l.rel)), ec);
  return ec;
}

}  // namespace

layer layer_of(const fs::mounts::route& r) { return layer{r.backend, r.rel, r.overlay}; }

std::optional<node> lookup(const layer& l) {
  if (auto info = info_of_host(data_of(l, l.rel))) {
    return node{origin::overlay, *info, data_of(l, l.rel)};
  }
  if (!archive_visible(l) || !l.backend) return std::nullopt;
  if (auto e = l.backend->lookup(l.rel)) {
    return node{origin::archive, *e, {}};
  }
  return std::nullopt;
}

std::optional<std::vector<archive::child>> list(const layer& l) {
  const auto self = lookup(l);
  if (!self || self->info.type != archive::kind::directory) return std::nullopt;

  std::map<std::string, archive::child> merged;
  // Archive children, unless the directory was recreated over them (opaque) or is hidden.
  if (l.backend && archive_visible(l) && !marker(opaque_of(l, l.rel))) {
    const auto e = l.backend->lookup(l.rel);
    if (e && e->type == archive::kind::directory) {
      if (auto kids = l.backend->list(l.rel)) {
        for (auto& c : *kids) {
          const auto child_rel = l.rel.empty() ? c.name : l.rel + "/" + c.name;
          if (marker(whiteout_of(l, child_rel))) continue;
          std::string name = c.name;
          merged.emplace(std::move(name), std::move(c));
        }
      }
    }
  }
  // Overlay children override archive children with the same name.
  const auto overlay_dir = data_of(l, l.rel);
  std::error_code ec;
  if (!overlay_dir.empty() && stdfs::is_directory(overlay_dir, ec)) {
    for (const auto& entry : stdfs::directory_iterator(overlay_dir, ec)) {
      const auto name = entry.path().filename().string();
      if (auto info = info_of_host(entry.path())) {
        merged[name] = archive::child{name, *info};
      }
    }
  }
  std::vector<archive::child> out;
  out.reserve(merged.size());
  for (auto& [name, c] : merged) out.push_back(std::move(c));
  return out;
}

std::error_code create_dir(const layer& l) {
  if (!has_overlay(l)) return ec_of(std::errc::read_only_file_system);
  if (lookup(l)) return ec_of(std::errc::file_exists);
  if (auto err = check_parent(l)) return err;
  if (auto err = make_parent_dirs(l)) return err;

  const bool had_whiteout = marker(whiteout_of(l, l.rel));
  remove_marker(whiteout_of(l, l.rel));
  std::error_code ec;
  stdfs::create_directory(data_of(l, l.rel), ec);
  if (ec) return ec;
  // A directory recreated over a deleted archive directory must hide that directory's children.
  if (had_whiteout) return make_marker(opaque_of(l, l.rel));
  return {};
}

std::error_code remove_file(const layer& l) {
  if (!has_overlay(l)) return ec_of(std::errc::read_only_file_system);
  const auto n = lookup(l);
  if (!n) return ec_of(std::errc::no_such_file_or_directory);
  if (n->info.type == archive::kind::directory) return ec_of(std::errc::is_a_directory);

  std::error_code ec;
  if (n->where == origin::overlay) stdfs::remove(data_of(l, l.rel), ec);
  if (ec) return ec;
  if (archive_has(l)) return make_marker(whiteout_of(l, l.rel));
  return {};
}

std::error_code remove_dir(const layer& l) {
  if (!has_overlay(l)) return ec_of(std::errc::read_only_file_system);
  const auto n = lookup(l);
  if (!n) return ec_of(std::errc::no_such_file_or_directory);
  if (n->info.type != archive::kind::directory) return ec_of(std::errc::not_a_directory);
  if (auto kids = list(l); kids && !kids->empty()) return ec_of(std::errc::directory_not_empty);

  std::error_code ec;
  if (n->where == origin::overlay) stdfs::remove(data_of(l, l.rel), ec);
  if (ec) return ec;
  // Markers below a removed directory are no longer needed, and would block its own marker.
  stdfs::remove_all(whiteout_of(l, l.rel), ec);
  stdfs::remove_all(opaque_of(l, l.rel), ec);
  if (archive_has(l)) return make_marker(whiteout_of(l, l.rel));
  return {};
}

std::error_code truncate(const layer& l, std::uintmax_t length) {
  if (!has_overlay(l)) return ec_of(std::errc::read_only_file_system);
  const auto n = lookup(l);
  if (!n) return ec_of(std::errc::no_such_file_or_directory);
  if (n->info.type == archive::kind::directory) return ec_of(std::errc::is_a_directory);
  if (n->where == origin::archive) {
    if (auto err = make_parent_dirs(l)) return err;
    if (auto err = copy_up(l, n->info)) return err;
  }
  std::error_code ec;
  stdfs::resize_file(data_of(l, l.rel), length, ec);
  return ec;
}

std::error_code rename_file(const layer& from, const layer& to) {
  if (!has_overlay(from) || !has_overlay(to)) return ec_of(std::errc::read_only_file_system);
  const auto nf = lookup(from);
  if (!nf) return ec_of(std::errc::no_such_file_or_directory);
  if (nf->info.type == archive::kind::directory) return ec_of(std::errc::not_supported);
  if (auto err = check_parent(to)) return err;
  const auto nt = lookup(to);
  if (nt && nt->info.type == archive::kind::directory) return ec_of(std::errc::is_a_directory);

  if (auto err = make_parent_dirs(from)) return err;
  if (nf->where == origin::archive) {
    if (auto err = copy_up(from, nf->info)) return err;
  }
  std::error_code ec;
  stdfs::create_directories(data_of(to, parent_of(to.rel)), ec);
  if (ec) return ec;
  remove_marker(whiteout_of(to, to.rel));
  stdfs::rename(data_of(from, from.rel), data_of(to, to.rel), ec);
  if (ec) return ec;
  if (archive_has(from)) return make_marker(whiteout_of(from, from.rel));
  return {};
}

std::expected<stdfs::path, std::error_code> prepare_write(const layer& l, bool create,
                                                          bool exclusive, bool truncate) {
  if (!has_overlay(l)) return std::unexpected(ec_of(std::errc::read_only_file_system));
  const auto n = lookup(l);
  if (n && exclusive && create) return std::unexpected(ec_of(std::errc::file_exists));
  if (n && n->info.type == archive::kind::directory) {
    return std::unexpected(ec_of(std::errc::is_a_directory));
  }
  if (!n && !create) return std::unexpected(ec_of(std::errc::no_such_file_or_directory));
  if (!n) {
    if (auto err = check_parent(l)) return std::unexpected(err);
    if (auto err = make_parent_dirs(l)) return std::unexpected(err);
    remove_marker(whiteout_of(l, l.rel));
    if (auto err = make_marker(data_of(l, l.rel))) return std::unexpected(err);
    return data_of(l, l.rel);
  }
  if (n->where == origin::archive) {
    if (auto err = make_parent_dirs(l)) return std::unexpected(err);
    if (truncate) {
      if (auto err = make_marker(data_of(l, l.rel))) return std::unexpected(err);
    } else if (auto err = copy_up(l, n->info)) {
      return std::unexpected(err);
    }
  }
  return data_of(l, l.rel);
}

std::expected<stdfs::path, std::error_code> materialize(const layer& l) {
  if (!has_overlay(l)) return std::unexpected(ec_of(std::errc::read_only_file_system));
  const auto n = lookup(l);
  if (!n) return std::unexpected(ec_of(std::errc::no_such_file_or_directory));
  if (n->info.type == archive::kind::directory) {
    return std::unexpected(ec_of(std::errc::is_a_directory));
  }
  if (n->where == origin::archive) {
    if (auto err = make_parent_dirs(l)) return std::unexpected(err);
    if (auto err = copy_up(l, n->info)) return std::unexpected(err);
  }
  return data_of(l, l.rel);
}

// Metadata changes apply to files only. Directories are never copied up as a tree.
std::error_code change_file_metadata(const layer& l, std::error_code (*apply)(const stdfs::path&, std::uint64_t),
                                     std::uint64_t value) {
  if (!has_overlay(l)) return ec_of(std::errc::read_only_file_system);
  const auto n = lookup(l);
  if (!n) return ec_of(std::errc::no_such_file_or_directory);
  if (n->info.type != archive::kind::file) return ec_of(std::errc::operation_not_permitted);
  const auto host = materialize(l);
  if (!host) return host.error();
  return apply(*host, value);
}

std::error_code set_mode(const layer& l, std::uint32_t mode_bits) {
  return change_file_metadata(
      l, [](const stdfs::path& p, std::uint64_t v) {
        return host_meta::set_mode(p, static_cast<std::uint32_t>(v));
      },
      mode_bits);
}

std::error_code set_mtime(const layer& l, std::int64_t seconds) {
  return change_file_metadata(
      l, [](const stdfs::path& p, std::uint64_t v) {
        return host_meta::set_mtime(p, static_cast<std::int64_t>(v));
      },
      static_cast<std::uint64_t>(seconds));
}

}  // namespace rwzfs::overlay::cow
