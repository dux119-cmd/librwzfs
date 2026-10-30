#include "rwzfs/fs/mount_table.hpp"

#include <algorithm>
#include <mutex>
#include <vector>

namespace rwzfs::fs::mounts {

namespace {

namespace fs = std::filesystem;

std::mutex& table_mutex() {
  static std::mutex m;
  return m;
}

std::vector<fs::path>& table() {
  static std::vector<fs::path> points;
  return points;
}

bool is_within(const fs::path& target, const fs::path& mountpoint) {
  const auto relative = target.lexically_relative(mountpoint);
  return !relative.empty() && *relative.begin() != fs::path("..");
}

}  // namespace

bool add(const fs::path& mountpoint) {
  if (!mountpoint.is_absolute()) {
    return false;
  }
  const auto normalized = mountpoint.lexically_normal();
  const std::scoped_lock lock(table_mutex());
  table().push_back(normalized);
  return true;
}

void clear() {
  const std::scoped_lock lock(table_mutex());
  table().clear();
}

bool is_archive_path(const fs::path& p) {
  const auto target = p.lexically_normal();
  const std::scoped_lock lock(table_mutex());
  return std::ranges::any_of(table(), [&](const fs::path& mp) { return is_within(target, mp); });
}

}  // namespace rwzfs::fs::mounts
