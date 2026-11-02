#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "rwzfs/archive/backend.hpp"
#include "rwzfs/fs/mount_table.hpp"

// Copy-on-write layer over a read-only archive. Writes never touch the archive.
//
// Overlay layout (under the overlay directory, see docs/overlay_design.md):
//   data/<rel>       files and directories created or copied up from the archive
//   whiteouts/<rel>  marker: the archive entry <rel> is deleted
//   opaque/<rel>     marker: directory <rel> was recreated; its archive children are hidden
//
// An overlay directory of "" means no overlay: mutations report EROFS.
// The layer trusts its inputs: paths and overlay locations are not validated here.
namespace rwzfs::overlay::cow {

struct layer {
  std::shared_ptr<const archive::backend> backend;
  std::string rel;               // archive-relative text, "" for the mount root
  std::filesystem::path overlay;  // overlay directory, or empty for none
};

[[nodiscard]] layer layer_of(const fs::mounts::route& r);

enum class origin { archive, overlay };

struct node {
  origin where{origin::archive};
  archive::entry info{};
  std::filesystem::path host;  // overlay origin only: the host file or directory
};

// Current state of an entry, or nullopt if it does not exist.
[[nodiscard]] std::optional<node> lookup(const layer& l);

// Merged children of a directory, sorted by name. nullopt if not a directory.
[[nodiscard]] std::optional<std::vector<archive::child>> list(const layer& l);

// Mutations. Each returns an error code (empty on success).
[[nodiscard]] std::error_code create_dir(const layer& l);
[[nodiscard]] std::error_code remove_file(const layer& l);
[[nodiscard]] std::error_code remove_dir(const layer& l);
[[nodiscard]] std::error_code truncate(const layer& l, std::uintmax_t length);
[[nodiscard]] std::error_code rename_file(const layer& from, const layer& to);

// Prepares an overlay file for writing and returns its host path. Callers then open it with the
// host open or fopen. create: create if missing. exclusive: fail if present. truncate: empty it.
[[nodiscard]] std::expected<std::filesystem::path, std::error_code> prepare_write(
    const layer& l, bool create, bool exclusive, bool truncate);

}  // namespace rwzfs::overlay::cow
