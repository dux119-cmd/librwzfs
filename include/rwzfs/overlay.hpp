#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace rwzfs {

// Overlay (copy-on-write) storage for a mounted archive. Writes never change the archive. They
// are stored under a per-archive directory, `<write root>/<stem>-writes`, where <stem> is the
// archive file name with everything from its first dot onward removed (a leading dot is kept).
// Example: "/data/archive.zip" -> "archive-writes".
struct mount_options {
  // Top-level writable directory. Must be absolute. When unset, the user state area is used:
  //   Linux, BSD:  $XDG_STATE_HOME/rwz if XDG_STATE_HOME is an absolute path,
  //                otherwise $HOME/.local/state/rwz
  //   macOS:       $HOME/Library/Application Support/rwz
  //   Windows:     %LOCALAPPDATA%\rwz
  std::optional<std::filesystem::path> write_root;
};

namespace overlay {

// Environment lookup, injectable for tests. Returns nullopt when the variable is unset.
using env_lookup = std::function<std::optional<std::string>(const char*)>;

enum class overlay_error {
  write_root_relative,  // the given write root is not absolute
  user_state_unknown,   // no user state directory: the needed variable is not set
};

// Extension-stripped archive name used for the overlay directory.
[[nodiscard]] std::string archive_stem(const std::filesystem::path& archive);

// Top-level writable directory for the default location.
[[nodiscard]] std::expected<std::filesystem::path, overlay_error> default_write_root(
    const env_lookup& env);

// Overlay directory for an archive: `<write root>/<stem>-writes`.
[[nodiscard]] std::expected<std::filesystem::path, overlay_error> overlay_directory(
    const std::filesystem::path& archive, const std::optional<std::filesystem::path>& write_root,
    const env_lookup& env);

// Environment lookup backed by std::getenv.
[[nodiscard]] env_lookup process_environment();

}  // namespace overlay
}  // namespace rwzfs
