#pragma once

#include <expected>
#include <filesystem>
#include <string>

#include "rwzfs/archive/create.hpp"

namespace rwzfs {

enum class revise_error {
  mount_not_found,  // nothing is mounted at the mountpoint, or the mount has no archive path
  no_changes,       // the overlay is empty (or there is no overlay): nothing to revise
  flatten_failed,   // the merged view could not be written to the scratch tree
  create_failed,    // the new archive could not be created (see create_archive)
  io,               // the scratch tree could not be created
};

// Name of the next revision: "archive.dwarfs" -> "archive-rev1.dwarfs". If the stem already ends
// in "-rev<N>", N is bumped: "archive-rev1.dwarfs" -> "archive-rev2.dwarfs". The extension is
// everything from the first dot, as in the overlay stem rule.
[[nodiscard]] std::string next_revision_name(const std::filesystem::path& archive_file);

// Revises the archive mounted at `mountpoint`:
//   1. flattens the merged view (overlay wins, whiteouts applied) into a scratch tree,
//   2. creates the next revision next to the archive with create_archive,
//   3. unmounts (releasing the lock) and clears the old overlay's data, whiteouts and opaque
//      markers.
// Returns the path of the new archive. The caller mounts it to continue.
[[nodiscard]] std::expected<std::filesystem::path, revise_error> revise_archive(
    const std::filesystem::path& mountpoint, const create_options& options = {});

}  // namespace rwzfs
