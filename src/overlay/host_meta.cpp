#include "rwzfs/overlay/host_meta.hpp"

#if !defined(_WIN32)

#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

#include <cerrno>
#include <cstring>

namespace rwzfs::overlay::host_meta {

namespace {

constexpr std::uint32_t kPermMask = 07777;

}  // namespace

std::optional<archive::entry> entry_of(const std::filesystem::path& p) {
  struct stat st {};
  if (p.empty() || ::lstat(p.c_str(), &st) != 0) return std::nullopt;
  archive::entry e;
  e.inode = static_cast<std::uint64_t>(st.st_ino);
  e.size = S_ISREG(st.st_mode) ? static_cast<std::uint64_t>(st.st_size) : 0;
  e.mode = static_cast<std::uint32_t>(st.st_mode);
  e.mtime = static_cast<std::int64_t>(st.st_mtime);
  if (S_ISREG(st.st_mode)) {
    e.type = archive::kind::file;
  } else if (S_ISDIR(st.st_mode)) {
    e.type = archive::kind::directory;
  } else if (S_ISLNK(st.st_mode)) {
    e.type = archive::kind::symlink;
  } else {
    e.type = archive::kind::other;
  }
  return e;
}

std::error_code set_mode(const std::filesystem::path& p, std::uint32_t mode_bits) {
  if (::chmod(p.c_str(), static_cast<mode_t>(mode_bits & kPermMask)) != 0) {
    return std::error_code(errno, std::generic_category());
  }
  return {};
}

std::error_code set_mtime(const std::filesystem::path& p, std::int64_t seconds) {
  struct timespec times[2];
  times[0].tv_sec = 0;
  times[0].tv_nsec = UTIME_OMIT;  // leave atime alone
  times[1].tv_sec = static_cast<time_t>(seconds);
  times[1].tv_nsec = 0;
  if (::utimensat(AT_FDCWD, p.c_str(), times, 0) != 0) {
    return std::error_code(errno, std::generic_category());
  }
  return {};
}

}  // namespace rwzfs::overlay::host_meta

#else  // Windows: permissions and timestamps through std::filesystem. Not yet verified.

#include "rwzfs/fs/time.hpp"

namespace rwzfs::overlay::host_meta {

namespace stdfs = std::filesystem;

std::optional<archive::entry> entry_of(const stdfs::path& p) {
  std::error_code ec;
  const auto st = stdfs::symlink_status(p, ec);
  if (ec || st.type() == stdfs::file_type::not_found) return std::nullopt;
  archive::entry e;
  e.type = st.type() == stdfs::file_type::regular     ? archive::kind::file
           : st.type() == stdfs::file_type::directory ? archive::kind::directory
                                                      : archive::kind::other;
  e.size = e.type == archive::kind::file ? stdfs::file_size(p, ec) : 0;
  e.mtime = rwzfs::fs::to_unix_seconds(stdfs::last_write_time(p, ec));
  e.mode = static_cast<std::uint32_t>(st.permissions());
  return e;
}

std::error_code set_mode(const stdfs::path& p, std::uint32_t mode_bits) {
  std::error_code ec;
  stdfs::permissions(p, static_cast<stdfs::perms>(mode_bits & 07777), stdfs::perm_options::replace, ec);
  return ec;
}

std::error_code set_mtime(const stdfs::path& p, std::int64_t seconds) {
  std::error_code ec;
  stdfs::last_write_time(p, rwzfs::fs::from_unix_seconds(seconds), ec);
  return ec;
}

}  // namespace rwzfs::overlay::host_meta

#endif
