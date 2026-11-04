#include "rwzfs/io/win32.hpp"

#if defined(_WIN32)

#include <windows.h>

#include <filesystem>
#include <string>

#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/fs/resolve.hpp"

namespace rwzfs::io::win32 {

namespace fs = rwzfs::fs;

namespace {

// Resolves a caller path and reports whether it is an archive path. Archive paths are not
// supported through Win32 yet.
bool is_archive(const wchar_t* text) {
  std::error_code ec;
  const auto p = fs::resolve(std::filesystem::path(text), ec);
  if (ec) {
    SetLastError(ERROR_INVALID_NAME);
    return true;
  }
  if (fs::mounts::find(p).has_value()) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return true;
  }
  return false;
}

}  // namespace

void* create_file(const wchar_t* name, std::uint32_t access, std::uint32_t share, void* security,
                  std::uint32_t disposition, std::uint32_t flags, void* template_file) {
  if (is_archive(name)) return INVALID_HANDLE_VALUE;
  return CreateFileW(name, access, share, static_cast<LPSECURITY_ATTRIBUTES>(security),
                     disposition, flags, static_cast<HANDLE>(template_file));
}

bool read_file(void* handle, void* buf, std::uint32_t count, std::uint32_t* bytes_read,
               void* overlapped) {
  return ReadFile(static_cast<HANDLE>(handle), buf, count, reinterpret_cast<LPDWORD>(bytes_read),
                  static_cast<LPOVERLAPPED>(overlapped)) != 0;
}

std::uint32_t set_file_pointer(void* handle, std::int32_t distance, std::int32_t* distance_high,
                               std::uint32_t method) {
  return SetFilePointer(static_cast<HANDLE>(handle), distance, reinterpret_cast<PLONG>(distance_high),
                        method);
}

void* find_first_file(const wchar_t* pattern, void* find_data) {
  if (is_archive(pattern)) return INVALID_HANDLE_VALUE;
  return FindFirstFileW(pattern, static_cast<LPWIN32_FIND_DATAW>(find_data));
}

bool create_directory(const wchar_t* path, void* security) {
  if (is_archive(path)) return false;
  return CreateDirectoryW(path, static_cast<LPSECURITY_ATTRIBUTES>(security)) != 0;
}

bool close_handle(void* handle) { return CloseHandle(static_cast<HANDLE>(handle)) != 0; }

}  // namespace rwzfs::io::win32

#endif  // defined(_WIN32)
