#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace rwzfs {

// Compression used when the caller does not choose one (the same flags as the mkdwarfs CLI).
// wlog and strat need a dwarfs build whose zstd exposes those parameters.
inline constexpr const char* kDefaultCompression = "zstd:level=12:wlog=27:strat=3";

struct create_options {
  std::string compression{kDefaultCompression};
  // mkdwarfs executable. When unset: the path given at build time, else "mkdwarfs" on PATH.
  std::optional<std::filesystem::path> mkdwarfs;
};

enum class create_error {
  source_missing,       // the source is not an existing directory
  tool_not_found,       // mkdwarfs could not be run
  tool_failed,          // mkdwarfs ran and failed (its message is on stderr)
  io,                   // the output could not be written or renamed
  unsupported_platform  // not implemented on this platform yet
};

// Builds an archive from `source_dir` with mkdwarfs, using the CLI flags:
//   --force --progress=simple --categorize --no-history --compression=<compression>
// It writes to `<output>.tmp` and renames it to `output` on success, so a failed run never
// leaves a partial archive at `output`.
[[nodiscard]] std::expected<void, create_error> create_archive(
    const std::filesystem::path& source_dir, const std::filesystem::path& output,
    const create_options& options = {});

}  // namespace rwzfs
