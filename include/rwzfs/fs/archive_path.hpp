#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace rwzfs::fs {

enum class archive_path_error {
  escapes_root,
};

// PATH-15: lexical normalization of an archive-relative path. See docs/archive_paths.md.
[[nodiscard]] std::expected<std::string, archive_path_error> normalize_archive_path(
    std::string_view text);

// The magic absolute root of the archive namespace: "/rwz" on POSIX, "C:/RWZ" on Windows.
[[nodiscard]] std::filesystem::path archive_root();

// Archive-relative text -> absolute archive path under archive_root().
[[nodiscard]] std::expected<std::filesystem::path, archive_path_error> to_archive_absolute(
    std::string_view text);

// Absolute path -> archive-relative text ("" for the root). nullopt if outside archive_root().
[[nodiscard]] std::optional<std::string> to_archive_relative(const std::filesystem::path& absolute);

}  // namespace rwzfs::fs
