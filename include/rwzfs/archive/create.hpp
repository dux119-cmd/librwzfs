#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace rwz {

// Compression used when the caller does not choose one. wlog and strat need a
// zstd build that exposes those parameters (the dwarfs-bundled zstd does).
inline constexpr const char* kDefaultCompression = "zstd:level=12:wlog=27:strat=3";

struct create_options {
	std::string compression{kDefaultCompression};
};

enum class create_error {
	source_missing, // the source is not an existing directory (or nothing
	                // is mounted there)
	tool_failed,    // the writer failed, e.g. an unknown compression option
	                // (see the diagnostic stream)
	io,             // the output could not be written or renamed
	unsupported_platform // needs the dwarfs writer, which this build does
	                     // not include
};

// Builds an archive of the host directory `source_dir`, in-process with the
// dwarfs writer (no mkdwarfs binary). It writes to `<output>.tmp` and renames
// it to `output` on success, so a failed run never leaves a partial archive at
// `output`.
[[nodiscard]] std::expected<void, create_error> create_archive(
        const std::filesystem::path& source_dir,
        const std::filesystem::path& output, const create_options& options = {});

} // namespace rwz
