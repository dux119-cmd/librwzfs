#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>

namespace rwz {

// Compression used when the caller does not choose one. wlog and strat need a
// zstd build that exposes those parameters (the dwarfs-bundled zstd does). The
// dwarfs level-7 profile (mkdwarfs -l 7): zstd:level=22 for data (the
// per-category overrides below apply to pcmaudio and fits), zstd:level=16 for
// the schema, zstd:level=22 for metadata, window size 12, window step 3, and
// nilsimsa inode ordering. The one override is the block size: 64 KiB (level 7
// uses 16 MiB, 2^24).
inline constexpr const char* kDefaultCompression = "zstd:level=22";
inline constexpr unsigned kDefaultBlockSizeBits  = 16;

// A snapshot of an archive being written, passed to create_options::on_progress.
// Only public information: the writer's internal counters are not visible to
// callers, so progress is the archive file's size so far.
struct create_progress {
	std::uint64_t bytes_written = 0;      // size of the archive file being
	                                      // written so far
	std::chrono::milliseconds elapsed{0}; // time since the write began
	bool final = false; // true for the last call, after the writer has
	                    // finished
};

struct create_options {
	std::string compression{kDefaultCompression};    // data blocks
	unsigned block_size_bits{kDefaultBlockSizeBits}; // block size as a power
	                                                 // of two (16 = 64 KiB)
	// Messages from the archive writer (dwarfs's own log), one per line.
	// Null: silent.
	std::ostream* log = nullptr;
	// Called about every progress_interval while writing, and once more
	// when done (create_progress::final).
	std::function<void(const create_progress&)> on_progress;
	std::chrono::milliseconds progress_interval{2000};
};

enum class create_error {
	source_missing, // the source is not an existing directory (or nothing
	                // is mounted there)
	tool_failed,    // the writer failed, e.g. an unknown compression option
	                // (see the diagnostic stream)
	io,             // the output could not be written or renamed
	unsupported_platform, // needs the dwarfs writer, which this build does
	                      // not include
	already_exists        // the output file exists; nothing was written
};

// Builds an archive of the host directory `source_dir`, in-process with the
// dwarfs writer (no mkdwarfs binary). It writes to `<output>.tmp` and renames
// it to `output` on success, so a failed run never leaves a partial archive at
// `output`.
[[nodiscard]] std::expected<void, create_error> create_archive(
        const std::filesystem::path& source_dir,
        const std::filesystem::path& output, const create_options& options = {});

} // namespace rwz
