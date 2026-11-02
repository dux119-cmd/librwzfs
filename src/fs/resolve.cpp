#include "rwzfs/fs/resolve.hpp"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "rwzfs/archive/backend.hpp"
#include "rwzfs/fs/archive_path.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/fs/walk.hpp"
#include "rwzfs/overlay/cow.hpp"

namespace rwzfs::fs {

namespace stdfs = std::filesystem;

namespace {

// Target text if `candidate` (absolute, inside the archive) is a symlink.
std::optional<std::string> archive_link_target(const std::vector<std::string>& candidate) {
  const auto p = detail::absolute_from(candidate, 0);
  const auto r = mounts::find(p);
  if (!r || !r->backend) return std::nullopt;
  // The merged view decides: an overlay file shadows an archive symlink, and overlay entries are never links.
  const auto n = overlay::cow::lookup(overlay::cow::layer_of(*r));
  if (!n || n->where != overlay::cow::origin::archive || n->info.type != archive::kind::symlink) {
    return std::nullopt;
  }
  return r->backend->link_target(r->rel);
}

// Walks an absolute path one component at a time, so a symlink in the middle of the path
// is followed before the next component (as the OS does, so "link/.." is correct).
stdfs::path resolve_components(const stdfs::path& input, std::error_code& ec, bool follow_final) {
  const auto root = detail::components(archive_root().generic_string());
  std::deque<std::string> pending;
  for (auto& c : detail::components(input.generic_string())) pending.push_back(c);

  std::vector<std::string> out;
  int follows = 0;
  while (!pending.empty()) {
    std::string token = std::move(pending.front());
    pending.pop_front();
    if (token == ".") continue;

    if (token == "..") {
      if (!detail::has_prefix(out, root)) {
        // Host path: pass ".." to the OS raw so host symlinks are followed.
        out.push_back(token);
        out.insert(out.end(), pending.begin(), pending.end());
        ec.clear();
        return detail::absolute_from(out, 0);
      }
      if (out.size() > root.size()) {
        out.pop_back();
        continue;
      }
      // Climbs above the archive root: the rest is a host path, passed raw.
      ec.clear();
      return detail::absolute_from(std::vector<std::string>(pending.begin(), pending.end()), 0);
    }

    auto candidate = out;
    candidate.push_back(token);
    const bool is_final = pending.empty();
    const bool inside_archive = detail::has_prefix(candidate, root) && candidate.size() > root.size();
    if (!inside_archive || (is_final && !follow_final)) {
      out.push_back(token);
      continue;
    }

    const auto target = archive_link_target(candidate);
    if (!target) {
      out.push_back(token);
      continue;
    }
    if (++follows > kMaxSymlinkFollows) {
      ec = std::make_error_code(std::errc::too_many_symbolic_link_levels);
      return {};
    }
    // An absolute target restarts the walk from "/". A relative one continues from this directory.
    if (stdfs::path(*target).is_absolute()) out.clear();
    const auto parts = detail::components(*target);
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) pending.push_front(*it);
  }
  ec.clear();
  return detail::absolute_from(out, 0);
}

stdfs::path resolve_with(const stdfs::path& p, std::error_code& ec, bool follow_final) {
  if (p.is_absolute()) return resolve_components(p, ec, follow_final);
  const auto base = cwd::archive_cwd();
  if (!base) {
    ec.clear();
    return p;  // relative host path: the OS resolves it against the host working directory
  }
  return resolve_components(*base / p, ec, follow_final);
}

}  // namespace

stdfs::path resolve_lexical(const stdfs::path& p) {
  if (p.is_absolute()) return walk_lexically(p);
  const auto base = cwd::archive_cwd();
  if (!base) return p;
  return walk_lexically(*base / p);
}

stdfs::path resolve(const stdfs::path& p, std::error_code& ec) {
  return resolve_with(p, ec, true);
}

stdfs::path resolve_no_follow(const stdfs::path& p, std::error_code& ec) {
  return resolve_with(p, ec, false);
}

}  // namespace rwzfs::fs
