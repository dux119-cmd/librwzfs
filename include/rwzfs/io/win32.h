#pragma once

#if defined(_WIN32)

// Win32 handles and structs are passed as opaque pointers so this header
// does not pull in <windows.h> and its macros.
#include <cstdint>

namespace rwz::win32 {

// WIN-01 (3x) CreateFileW. Handle is a HANDLE passed as void*.
[[nodiscard]] void* create_file(const wchar_t* name, std::uint32_t access,
                                std::uint32_t share, void* security,
                                std::uint32_t disposition, std::uint32_t flags,
                                void* template_file);
// WIN-02 (2x) ReadFile
[[nodiscard]] bool read_file(void* handle, void* buf, std::uint32_t count,
                             std::uint32_t* bytes_read, void* overlapped);
// WIN-03 (11x) SetFilePointer
[[nodiscard]] std::uint32_t set_file_pointer(void* handle, std::int32_t distance,
                                             std::int32_t* distance_high,
                                             std::uint32_t method);
// WIN-04 (2x) FindFirstFileW. find_data is a WIN32_FIND_DATAW passed as void*.
[[nodiscard]] void* find_first_file(const wchar_t* pattern, void* find_data);
// WIN-05 (1x) CreateDirectoryW
[[nodiscard]] bool create_directory(const wchar_t* path, void* security);

// Helpers not in the call list: FindNextFileW and FindClose, for handles from
// find_first_file.
[[nodiscard]] bool find_next_file(void* find, void* find_data);
[[nodiscard]] bool find_close(void* find);

// DeleteFileW. On archive paths the file is hidden in the overlay (a whiteout),
// as unlink does. RemoveDirectoryW. On archive paths an empty directory is
// hidden in the overlay; a non-empty one fails with ERROR_DIR_NOT_EMPTY.
[[nodiscard]] bool remove_directory(const wchar_t* path);
[[nodiscard]] bool delete_file(const wchar_t* path);

// CloseHandle for handles returned by create_file.
[[nodiscard]] bool close_handle(void* handle);

// Host paths call the Win32 API. Archive paths: read-only opens of archive files
// return a handle from the archive table, writes copy the file up into the
// overlay, directories and FindFirstFileW lists the merged view of an archive
// directory (archive plus overlay), with "." and "..". Directory handles from
// create_file are ERROR_NOT_SUPPORTED. Failure values follow each Win32 function.

} // namespace rwz::win32

#endif // defined(_WIN32)
