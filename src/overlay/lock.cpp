#include "rwzfs/overlay/lock.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace rwzfs::overlay::lock {

namespace stdfs = std::filesystem;

namespace {

constexpr std::size_t kMaxHeld = 64;
constexpr std::size_t kPathCapacity = 4096;

std::mutex& held_mutex() {
  static std::mutex m;
  return m;
}

// Lock file paths held by this process. The signal handler reads the fixed-size slots below,
// so it never touches a std::string or allocates.
std::vector<std::string>& held() {
  static std::vector<std::string> paths;
  return paths;
}

struct slot {
  char path[kPathCapacity];
  bool used;
};

slot* slots() {
  static slot table[kMaxHeld] = {};
  return table;
}

void record(const std::string& path) {
  held().push_back(path);
  for (std::size_t i = 0; i < kMaxHeld; ++i) {
    if (!slots()[i].used && path.size() < kPathCapacity) {
      std::memcpy(slots()[i].path, path.c_str(), path.size() + 1);
      slots()[i].used = true;
      return;
    }
  }
}

void forget(const std::string& path) {
  std::erase(held(), path);
  for (std::size_t i = 0; i < kMaxHeld; ++i) {
    if (slots()[i].used && path == slots()[i].path) slots()[i].used = false;
  }
}

#if !defined(_WIN32)

bool owner_alive(long pid) {
  if (pid <= 0) return false;
  if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
  return errno == EPERM;  // exists, owned by someone else
}

void unlink_all_held() noexcept {
  for (std::size_t i = 0; i < kMaxHeld; ++i) {
    if (slots()[i].used) ::unlink(slots()[i].path);
  }
}

void on_exit() noexcept { unlink_all_held(); }

void on_signal(int sig) {
  unlink_all_held();
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}

#endif

}  // namespace

std::expected<void, lock_error> acquire(const stdfs::path& overlay_dir) {
#if defined(_WIN32)
  // Windows locking is not implemented yet. See docs/overlay_design.md.
  (void)overlay_dir;
  return {};
#else
  static bool exit_hook_installed = [] {
    std::atexit(on_exit);
    return true;
  }();
  (void)exit_hook_installed;

  const auto path = (overlay_dir / kLockName).string();
  const std::scoped_lock lock(held_mutex());
  for (int attempt = 0; attempt < 2; ++attempt) {
    const int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd >= 0) {
      const auto text = std::to_string(static_cast<long>(::getpid())) + "\n";
      const bool wrote = ::write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
      ::close(fd);
      if (!wrote) {
        ::unlink(path.c_str());
        return std::unexpected(lock_error::io);
      }
      record(path);
      return {};
    }
    if (errno != EEXIST) return std::unexpected(lock_error::io);

    std::FILE* in = std::fopen(path.c_str(), "r");
    long pid = 0;
    if (in != nullptr) {
      if (std::fscanf(in, "%ld", &pid) != 1) pid = 0;
      std::fclose(in);
    }
    if (owner_alive(pid)) return std::unexpected(lock_error::in_use);
    ::unlink(path.c_str());  // stale: its owner is gone
  }
  return std::unexpected(lock_error::in_use);
#endif
}

void release(const stdfs::path& overlay_dir) noexcept {
  const auto path = (overlay_dir / kLockName).string();
  const std::scoped_lock lock(held_mutex());
  try {
    forget(path);
  } catch (...) {
    // forget() only erases from containers; nothing to recover from
  }
#if !defined(_WIN32)
  ::unlink(path.c_str());
#endif
}

void enable_signal_cleanup() {
#if !defined(_WIN32)
  for (int sig : {SIGINT, SIGTERM, SIGHUP}) std::signal(sig, on_signal);
#endif
}

}  // namespace rwzfs::overlay::lock
