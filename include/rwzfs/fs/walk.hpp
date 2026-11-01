#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rwzfs::fs {

// Lexical walk for absolute paths. Inside the archive root, "." and ".." are applied
// lexically. A ".." that leaves the archive, or any ".." on a host path, ends the lexical
// part: the rest of the path is returned raw so the OS follows host symlinks.
// Relative paths are returned unchanged. Archive symlinks are not followed here (see resolve.hpp).
[[nodiscard]] std::filesystem::path walk_lexically(const std::filesystem::path& p);

namespace detail {

// Splits generic path text on '/', dropping empty components.
[[nodiscard]] std::vector<std::string> components(std::string_view text);

// Renders components[from..] as an absolute path. A leading drive token ("C:") replaces the slash.
[[nodiscard]] std::filesystem::path absolute_from(const std::vector<std::string>& parts,
                                                  std::size_t from);

[[nodiscard]] bool has_prefix(const std::vector<std::string>& parts,
                              const std::vector<std::string>& prefix);

}  // namespace detail

}  // namespace rwzfs::fs
