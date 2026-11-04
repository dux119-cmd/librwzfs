#pragma once

#if !defined(_WIN32)

#include <glob.h>

#include <filesystem>

// glob() over the merged archive view. Supports the usual wildcards (*, ?, [...]) in any path
// component, sorted results, and GLOB_NOCHECK. GLOB_APPEND and GLOB_BRACE/GLOB_TILDE are not
// supported and fail with GLOB_ABORTED / ENOTSUP. Returns glob()'s codes.
namespace rwzfs::io::archive_glob {

[[nodiscard]] int glob(const std::filesystem::path& absolute_pattern, int flags, glob_t* out);

}  // namespace rwzfs::io::archive_glob

#endif  // !defined(_WIN32)
