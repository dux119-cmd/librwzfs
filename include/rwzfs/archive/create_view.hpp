#pragma once

#include <expected>
#include <filesystem>
#include <ostream>

#include "rwzfs/archive/create.hpp"

namespace rwz {

// Writes an archive of the merged view mounted at `root` (archive plus
// overlay). Content and directory structure are read through the library's own
// calls, so no temporary tree is made. The output is written to <output>.tmp
// and renamed when complete. Writer diagnostics go to `log` when given
// (nullptr: silent). Requires the dwarfs reader and writer (RWZFS_WITH_DWARFS);
// otherwise returns unsupported_platform.
[[nodiscard]] std::expected<void, create_error> create_archive_from_view(
        const std::filesystem::path& root, const std::filesystem::path& output,
        const create_options& options, std::ostream* log = nullptr);

} // namespace rwz
