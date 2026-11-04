#pragma once

#include <expected>
#include <filesystem>

namespace rwzfs::overlay::lock {

// Lock file name inside an overlay directory. It exists while the archive is open.
inline constexpr const char* kLockName = ".rwz.lock";

enum class lock_error {
  in_use,  // the lock file exists: one instance only, delete the lock file to continue
  io,      // the lock file could not be created or read
};

// Creates the lock file with O_EXCL and this process's id. If the lock file already exists the
// archive is refused (lock_error::in_use): only one instance may use it. Remove the lock file
// by hand to continue after a crash.
[[nodiscard]] std::expected<void, lock_error> acquire(const std::filesystem::path& overlay_dir);

// Removes the lock file and forgets it. Safe to call for a directory that is not locked.
void release(const std::filesystem::path& overlay_dir) noexcept;

// Opt-in: on SIGINT, SIGTERM and SIGHUP, remove the held lock files, then re-raise the signal
// with the default action. Best effort: the handler only calls unlink on paths recorded at
// acquire time. SIGKILL cannot be handled; stale locks are then taken over on the next mount.
void enable_signal_cleanup();

}  // namespace rwzfs::overlay::lock
