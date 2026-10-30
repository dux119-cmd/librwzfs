#pragma once

#include <filesystem>
#include <optional>
#include <system_error>

#include "rwzfs/fs/path.hpp"

namespace rwzfs::fs {

// CWD-05. Resolves p against the archive working directory when one is set.
// See docs/cwd_design.md.
[[nodiscard]] path resolve(const path& p);

// CWD-02/CWD-03. Changes the working directory. Archive targets are not supported
// until archive lookups exist.
void change_directory(const path& p, std::error_code& ec);

namespace cwd {

// Current archive working directory, or nullopt when the host one applies.
[[nodiscard]] std::optional<path> archive_cwd();

// Enters an archive directory. Returns false unless p is absolute and inside a mount.
// Internal: used by chdir after archive directory lookup succeeds, and by tests.
[[nodiscard]] bool set_archive_cwd(const path& p);

// Forgets the archive working directory, so the host one applies again.
void clear();

}  // namespace cwd

}  // namespace rwzfs::fs
