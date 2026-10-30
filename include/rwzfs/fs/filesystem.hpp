#pragma once

#include <filesystem>
#include <system_error>

#include "rwzfs/fs/path.hpp"

namespace rwzfs::fs {

// Every wrapper reports failure through `ec`, never by throwing.

// FS-01 (16x)
[[nodiscard]] bool exists(const path& p, std::error_code& ec);
// FS-02 (21x)
[[nodiscard]] std::filesystem::file_status status(const path& p, std::error_code& ec);
// FS-03 (16x)
void copy(const path& from, const path& to, std::filesystem::copy_options options,
          std::error_code& ec);
// FS-04 (15x)
[[nodiscard]] bool create_directories(const path& p, std::error_code& ec);
// FS-05 (11x)
[[nodiscard]] bool is_regular_file(const path& p, std::error_code& ec);
// FS-06 (10x)
[[nodiscard]] bool is_directory(const path& p, std::error_code& ec);
// FS-07 (10x)
[[nodiscard]] path absolute(const path& p, std::error_code& ec);
// FS-08 (9x)
[[nodiscard]] path canonical(const path& p, std::error_code& ec);
// FS-09 (7x)
[[nodiscard]] std::filesystem::directory_iterator directory_iterator(const path& p,
                                                                    std::error_code& ec);
// FS-10 (6x)
[[nodiscard]] bool is_empty(const path& p, std::error_code& ec);
// FS-11 (5x)
void permissions(const path& p, std::filesystem::perms perms, std::error_code& ec);
// FS-12 (4x)
[[nodiscard]] bool is_symlink(const path& p, std::error_code& ec);
// FS-13 (4x)
[[nodiscard]] bool equivalent(const path& a, const path& b, std::error_code& ec);
// FS-14 (2x) / CWD-01: returns the archive cwd when one is set.
[[nodiscard]] path current_path(std::error_code& ec);
// CWD-02 (new): sets the working directory. Overload of std::filesystem::current_path(p, ec).
void current_path(const path& p, std::error_code& ec);
// FS-15 (2x)
[[nodiscard]] path read_symlink(const path& p, std::error_code& ec);
// FS-16 (2x)
[[nodiscard]] bool create_directory(const path& p, std::error_code& ec);
// FS-17 (2x)
[[nodiscard]] std::filesystem::space_info space(const path& p, std::error_code& ec);
// FS-18 (1x)
[[nodiscard]] std::filesystem::recursive_directory_iterator recursive_directory_iterator(
    const path& p, std::error_code& ec);
// FS-19 (1x)
[[nodiscard]] path proximate(const path& p, const path& base, std::error_code& ec);
// FS-20 (1x)
[[nodiscard]] std::filesystem::file_time_type last_write_time(const path& p,
                                                              std::error_code& ec);
// FS-21 (1x)
[[nodiscard]] std::uintmax_t remove_all(const path& p, std::error_code& ec);
// FS-22 (1x)
void resize_file(const path& p, std::uintmax_t size, std::error_code& ec);
// FS-23 (1x) `fs::remove` (21x) is also covered here.
[[nodiscard]] bool remove(const path& p, std::error_code& ec);
// FS-24 (1x) `fs::rename`
void rename(const path& from, const path& to, std::error_code& ec);

}  // namespace rwzfs::fs
