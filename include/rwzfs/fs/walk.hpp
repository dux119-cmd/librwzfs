#pragma once

#include <filesystem>

namespace rwzfs::fs {

// Lexical walk for absolute paths. Inside the archive root, "." and ".." are applied
// lexically. A ".." that leaves the archive, or any ".." on a host path, ends the lexical
// part: the rest of the path is returned raw so the OS follows host symlinks.
// Relative paths are returned unchanged.
[[nodiscard]] std::filesystem::path walk_lexically(const std::filesystem::path& p);

}  // namespace rwzfs::fs
