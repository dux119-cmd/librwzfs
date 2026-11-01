#pragma once

#include <filesystem>
#include <system_error>

namespace rwzfs::fs {

// Maximum symlinks followed while resolving one path. One more fails with ELOOP.
inline constexpr int kMaxSymlinkFollows = 5;

// Makes a path absolute (applying the archive working directory) and resolves it with
// archive symlinks followed. Host symlinks are left to the OS. Lexical only: no symlinks followed.
[[nodiscard]] std::filesystem::path resolve_lexical(const std::filesystem::path& p);

// Like resolve_lexical, and follows symlinks inside archives, including the final component.
// Fails with too_many_symbolic_link_levels (ELOOP) after kMaxSymlinkFollows links.
[[nodiscard]] std::filesystem::path resolve(const std::filesystem::path& p,
                                            std::error_code& ec);

// Like resolve, but the final component is not followed (lstat-like). Used by
// is_symlink, read_symlink, remove, rename, unlink, rmdir, mkdir, create_directory.
[[nodiscard]] std::filesystem::path resolve_no_follow(const std::filesystem::path& p,
                                                      std::error_code& ec);

}  // namespace rwzfs::fs
