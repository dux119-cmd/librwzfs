#include "rwzfs/overlay/lock.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
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

// Lock file paths held by this process. The POSIX signal handler reads the fixed-size slots, so
// it never touches a std::string or allocates.
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

// Creates the file only if it does not exist, and writes this process id into it.
// Returns 0 on success, EEXIST if present, or another errno.
int create_exclusive(const std::string& path) {
#if defined(_WIN32)
  std::FILE* f = std::fopen(path.c_str(), "wx");  // "x": fail if the file exists
  if (f == nullptr) return errno;
  std::fprintf(f, "%lu\n", static_cast<unsigned long>(GetCurrentProcessId()));
  std::fclose(f);
  return 0;
#else
  const int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
  if (fd < 0) return errno;
  const auto text = std::to_string(static_cast<long>(::getpid())) + "\n";
  const bool wrote = ::write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
  ::close(fd);
  if (!wrote) {
    ::unlink(path.c_str());
    return EIO;
  }
  return 0;
#endif
}

void remove_file(const std::string& path) noexcept {
  std::remove(path.c_str());
}

void remove_held() noexcept {
  for (std::size_t i = 0; i < kMaxHeld; ++i) {
    if (slots()[i].used) remove_file(slots()[i].path);
  }
}

void on_exit() noexcept { remove_held(); }

#if !defined(_WIN32)
void on_signal(int sig) {
  remove_held();
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}
#endif

}  // namespace

std::expected<void, lock_error> acquire(const stdfs::path& overlay_dir) {
  static bool exit_hook_installed = [] {
    std::atexit(on_exit);
    return true;
  }();
  (void)exit_hook_installed;

  const auto path = (overlay_dir / kLockName).string();
  const std::scoped_lock lock(held_mutex());
  // The lock file is a breadcrumb honored by this library: while it exists, the archive is
  // refused. A crashed process leaves it behind; the user removes it to continue.
  const int err = create_exclusive(path);
  if (err == EEXIST) return std::unexpected(lock_error::in_use);
  if (err != 0) return std::unexpected(lock_error::io);
  record(path);
  return {};
}

void release(const stdfs::path& overlay_dir) noexcept {
  const auto path = (overlay_dir / kLockName).string();
  const std::scoped_lock lock(held_mutex());
  try {
    forget(path);
  } catch (...) {
    // forget() only erases from containers; nothing to recover from
  }
  remove_file(path);
}

void enable_signal_cleanup() {
#if !defined(_WIN32)
  for (int sig : {SIGINT, SIGTERM, SIGHUP}) std::signal(sig, on_signal);
#endif
}

}  // namespace rwzfs::overlay::lock
