#pragma once

#if defined(_WIN32)

// Win32 handles and structs are passed as opaque pointers so this header
// does not pull in <windows.h> and its macros.
#include <cstdint>

namespace rwzfs::io::win32 {

// WIN-01 (3x) CreateFileW. Handle is a HANDLE passed as void*.
[[nodiscard]] void* create_file(const wchar_t* name, std::uint32_t access, std::uint32_t share,
                                void* security, std::uint32_t disposition, std::uint32_t flags,
                                void* template_file);
// WIN-02 (2x) ReadFile
[[nodiscard]] bool read_file(void* handle, void* buf, std::uint32_t count,
                             std::uint32_t* bytes_read, void* overlapped);
// WIN-03 (11x) SetFilePointer
[[nodiscard]] std::uint32_t set_file_pointer(void* handle, std::int32_t distance,
                                             std::int32_t* distance_high, std::uint32_t method);
// WIN-04 (2x) FindFirstFileW. find_data is a WIN32_FIND_DATAW passed as void*.
[[nodiscard]] void* find_first_file(const wchar_t* pattern, void* find_data);
// WIN-05 (1x) CreateDirectoryW
[[nodiscard]] bool create_directory(const wchar_t* path, void* security);

// Helper not in the call list: CloseHandle for handles returned by create_file / find_first_file.
[[nodiscard]] bool close_handle(void* handle);

// Host paths call the Win32 API. Archive paths fail with ERROR_NOT_SUPPORTED (archive access
// through Win32 handles is not implemented yet). Failure values follow each Win32 function.

}  // namespace rwzfs::io::win32

#endif  // defined(_WIN32)
