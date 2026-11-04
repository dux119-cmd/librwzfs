#pragma once

#include <filesystem>
#include <system_error>

#include "rwzfs/overlay/cow.hpp"

namespace rwzfs::overlay {

// Writes the merged view of a layer (archive with overlay applied, whiteouts removed) as a plain
// host tree under `host_dir`, which must exist and be empty. Files, directories, archive symlinks,
// modes and modification times are reproduced. The source is never modified.
[[nodiscard]] std::error_code flatten(const cow::layer& root, const std::filesystem::path& host_dir);

}  // namespace rwzfs::overlay
