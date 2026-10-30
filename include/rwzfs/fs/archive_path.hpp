#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace rwzfs::fs {

enum class archive_path_error {
  escapes_root,
};

// PATH-15: lexical normalization of an archive-relative path. See docs/archive_paths.md.
[[nodiscard]] std::expected<std::string, archive_path_error> normalize_archive_path(
    std::string_view text);

}  // namespace rwzfs::fs
