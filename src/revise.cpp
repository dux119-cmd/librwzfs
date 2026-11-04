#include "rwzfs/revise.hpp"

#include <cctype>
#include <ctime>
#include <system_error>

#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/mount.hpp"
#include "rwzfs/overlay/cow.hpp"
#include "rwzfs/overlay/flatten.hpp"

namespace rwzfs {

namespace stdfs = std::filesystem;

namespace {

constexpr const char* kRevPrefix = "-rev";

// Splits "stem" + ext where ext starts at the first dot (skipping a leading dot).
void split_name(const std::string& name, std::string& stem, std::string& ext) {
  const auto dot = name.find('.', 1);
  stem = dot == std::string::npos ? name : name.substr(0, dot);
  ext = dot == std::string::npos ? std::string{} : name.substr(dot);
}

// If `stem` ends in "-rev<digits>", returns the number and strips the suffix from `base`.
bool take_revision(const std::string& stem, std::string& base, unsigned long& number) {
  const auto pos = stem.rfind(kRevPrefix);
  if (pos == std::string::npos || pos == 0) return false;
  const auto digits = stem.substr(pos + 4);
  if (digits.empty()) return false;
  for (const char c : digits) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  base = stem.substr(0, pos);
  number = std::stoul(digits);
  return true;
}

bool overlay_has_changes(const stdfs::path& overlay) {
  std::error_code ec;
  for (const char* area : {"data", "whiteouts", "opaque"}) {
    const auto dir = overlay / area;
    if (stdfs::is_directory(dir, ec) && !stdfs::is_empty(dir, ec)) return true;
  }
  return false;
}

void clear_overlay(const stdfs::path& overlay) {
  std::error_code ec;
  for (const char* area : {"data", "whiteouts", "opaque"}) {
    stdfs::remove_all(overlay / area, ec);
  }
}

}  // namespace

std::string next_revision_name(const stdfs::path& archive_file) {
  std::string stem;
  std::string ext;
  split_name(archive_file.filename().string(), stem, ext);
  std::string base = stem;
  unsigned long number = 0;
  if (take_revision(stem, base, number)) {
    return base + kRevPrefix + std::to_string(number + 1) + ext;
  }
  return stem + kRevPrefix + "1" + ext;
}

std::expected<stdfs::path, revise_error> revise_archive(const stdfs::path& mountpoint,
                                                        const create_options& options) {
  const auto archive = fs::mounts::archive_of(mountpoint);
  const auto route = fs::mounts::find(mountpoint);
  if (!archive || !route || !route->backend || !route->rel.empty()) {
    return std::unexpected(revise_error::mount_not_found);
  }
  if (route->overlay.empty() || !overlay_has_changes(route->overlay)) {
    return std::unexpected(revise_error::no_changes);
  }

  const auto archive_dir = archive->parent_path();
  const auto scratch = archive_dir / (".rwz-revise-" + std::to_string(std::time(nullptr)));
  std::error_code ec;
  if (!stdfs::create_directory(scratch, ec)) return std::unexpected(revise_error::io);

  const auto root = overlay::cow::layer{route->backend, "", route->overlay};
  if (overlay::flatten(root, scratch)) {
    stdfs::remove_all(scratch, ec);
    return std::unexpected(revise_error::flatten_failed);
  }

  const auto out = archive_dir / next_revision_name(*archive);
  const auto created = create_archive(scratch, out, options);
  stdfs::remove_all(scratch, ec);
  if (!created) return std::unexpected(revise_error::create_failed);

  // The new revision contains the overlay's changes, so the old mount and its overlay are retired.
  const auto overlay = route->overlay;
  unmount_archive(mountpoint);
  clear_overlay(overlay);
  return out;
}

}  // namespace rwzfs
