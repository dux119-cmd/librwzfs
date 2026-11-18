#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <system_error>

#include "rwzfs/archive/backend.hpp"

// Host metadata for overlay files, read without following symlinks.
namespace rwz::overlay::host_meta {

// Type, mode (st_mode with type bits), size and mtime of a host path, or
// nullopt if absent.
[[nodiscard]] std::optional<rwz::archive::entry> entry_of(const std::filesystem::path& p);

// Sets permission bits (0..07777) on a host file.
[[nodiscard]] std::error_code set_mode(const std::filesystem::path& p,
                                       std::uint32_t mode_bits);

// Sets the modification time (seconds since the Unix epoch). The access time is
// left unchanged.
[[nodiscard]] std::error_code set_mtime(const std::filesystem::path& p,
                                        std::int64_t seconds);

} // namespace rwz::overlay::host_meta
