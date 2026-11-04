#include "rwzfs/fs/mount_table.hpp"

#include <algorithm>
#include <mutex>
#include <vector>

#include "rwzfs/fs/archive_path.hpp"
#include "rwzfs/fs/cwd.hpp"
#include "rwzfs/fs/walk.hpp"
#include "rwzfs/overlay/lock.hpp"

namespace rwzfs::fs::mounts {

namespace {

namespace fs = std::filesystem;

struct mount_entry {
  fs::path mountpoint;
  std::shared_ptr<const archive::backend> backend;
  fs::path overlay;
  fs::path archive;
};

std::mutex& table_mutex() {
  static std::mutex m;
  return m;
}

std::vector<mount_entry>& table() {
  static std::vector<mount_entry> entries;
  return entries;
}

bool is_within(const fs::path& target, const fs::path& mountpoint) {
  const auto relative = target.lexically_relative(mountpoint);
  return !relative.empty() && *relative.begin() != fs::path("..");
}

}  // namespace

bool is_valid_mountpoint(const fs::path& p) {
  return p.is_absolute() && is_within(p.lexically_normal(), archive_root());
}

bool add(const fs::path& mountpoint, std::shared_ptr<const archive::backend> backend,
         fs::path overlay, fs::path archive) {
  if (!is_valid_mountpoint(mountpoint)) {
    return false;
  }
  const auto normalized = mountpoint.lexically_normal();
  {
    const std::scoped_lock lock(table_mutex());
    table().push_back({normalized, std::move(backend), std::move(overlay), std::move(archive)});
  }
  // Relative paths resolve into the archive by default once one is mounted.
  if (!cwd::archive_cwd()) {
    // Cannot fail: the path was validated as a mountpoint above.
    [[maybe_unused]] const bool entered = cwd::set_archive_cwd(normalized);
  }
  return true;
}

void clear() {
  std::vector<fs::path> overlays;
  {
    const std::scoped_lock lock(table_mutex());
    for (const auto& m : table()) {
      if (!m.overlay.empty()) overlays.push_back(m.overlay);
    }
    table().clear();
  }
  for (const auto& o : overlays) overlay::lock::release(o);
  cwd::clear();
}

std::optional<std::filesystem::path> archive_of(const fs::path& mountpoint) {
  const auto normalized = mountpoint.lexically_normal();
  const std::scoped_lock lock(table_mutex());
  for (const auto& m : table()) {
    if (m.mountpoint == normalized) return m.archive;
  }
  return std::nullopt;
}

std::optional<fs::path> remove(const fs::path& mountpoint) {
  const auto normalized = mountpoint.lexically_normal();
  std::optional<fs::path> overlay;
  {
    const std::scoped_lock lock(table_mutex());
    auto& entries = table();
    const auto it = std::ranges::find_if(
        entries, [&](const mount_entry& m) { return m.mountpoint == normalized; });
    if (it == entries.end()) return std::nullopt;
    overlay = it->overlay;
    entries.erase(it);
  }
  if (const auto base = cwd::archive_cwd(); base && is_within(*base, normalized)) {
    cwd::clear();
  }
  if (overlay && !overlay->empty()) overlay::lock::release(*overlay);
  return overlay ? *overlay : fs::path{};
}

bool is_archive_path(const fs::path& p) {
  if (!p.is_absolute()) {
    return false;
  }
  const auto target = walk_lexically(p);
  const std::scoped_lock lock(table_mutex());
  return std::ranges::any_of(table(), [&](const mount_entry& m) {
    return is_within(target, m.mountpoint);
  });
}

std::optional<route> find(const fs::path& p) {
  if (!p.is_absolute()) {
    return std::nullopt;
  }
  const auto target = walk_lexically(p);
  const std::scoped_lock lock(table_mutex());
  const mount_entry* best = nullptr;
  for (const auto& m : table()) {
    if (is_within(target, m.mountpoint) &&
        (best == nullptr || m.mountpoint.string().size() > best->mountpoint.string().size())) {
      best = &m;
    }
  }
  if (best == nullptr) {
    return std::nullopt;
  }
  auto rel = target.lexically_relative(best->mountpoint).generic_string();
  if (rel == ".") {
    rel.clear();
  }
  return route{best->backend, std::move(rel), target, best->overlay, best->archive};
}

}  // namespace rwzfs::fs::mounts
