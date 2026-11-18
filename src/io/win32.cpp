#include "rwzfs/io/win32.hpp"

#if defined(_WIN32)

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "rwzfs/archive/backend.hpp"
#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/fs/resolve.hpp"
#include "rwzfs/overlay/cow.hpp"

// Archive paths through Win32:
//   - Read-only opens of archive-only files return a handle from this module's
//   table. Reads and
//     seeks use the archive; the table entry is removed by close_handle.
//   - Overlay entries (and all host paths) use real Win32 handles, so reads,
//   writes and seeks are
//     native.
//   - Writes to archive paths copy the file up first (no deferral on Windows),
//   then open the
//     overlay copy.
//   - FindFirstFileW on an archive directory lists the merged view, with "."
//   and "..". Find handles
//     come from this module's table; find_next_file and find_close accept them.
// Mirrors the POSIX behaviour in src/io/overlay_posix.cpp and
// src/io/archive_io.cpp.

namespace rwz::win32 {

namespace fs  = rwz::fs;
namespace cow = rwz::overlay::cow;

namespace {

struct archive_handle {
	std::shared_ptr<const rwz::archive::backend> backend;
	std::uint64_t inode{0};
	std::uint64_t size{0};
	std::uint64_t offset{0};
	bool directory{false}; // a directory handle: reads and seeks fail, as
	                       // on Windows
};

std::mutex& table_mutex()
{
	static std::mutex m;
	return m;
}

std::map<std::uintptr_t, archive_handle>& table()
{
	static std::map<std::uintptr_t, archive_handle> t;
	return t;
}

// Handle values for archive entries: not real handles, but never 0 or
// INVALID_HANDLE_VALUE.
std::uintptr_t allocate_id()
{
	static std::uintptr_t next = 0x70000000;
	next += 4;
	return next;
}

void* to_handle(std::uintptr_t id)
{
	return reinterpret_cast<void*>(id);
}
std::uintptr_t from_handle(void* h)
{
	return reinterpret_cast<std::uintptr_t>(h);
}

DWORD win_error_of(const std::error_code& ec)
{
	switch (ec.value()) {
	case ENOENT: return ERROR_FILE_NOT_FOUND;
	case EEXIST: return ERROR_FILE_EXISTS;
	case ENOTDIR: return ERROR_PATH_NOT_FOUND;
	case ENOTEMPTY: return ERROR_DIR_NOT_EMPTY;
	case EROFS: return ERROR_WRITE_PROTECT;
	case EINVAL: return ERROR_INVALID_PARAMETER;
	default: return ERROR_ACCESS_DENIED;
	}
}

enum class route_kind { host, archive, invalid };

// Classifies a caller path. For archive paths, `out` receives the route.
route_kind classify(const wchar_t* name, std::optional<rwz::fs::mounts::route>& out)
{
	std::error_code ec;
	const auto p = rwz::fs::resolve(std::filesystem::path(name), ec);
	if (ec) {
		return route_kind::invalid;
	}
	auto r = rwz::fs::mounts::find(p);
	if (!r) {
		return route_kind::host;
	}
	out = std::move(r);
	return route_kind::archive;
}

bool wants_write(std::uint32_t access)
{
	return (access & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0;
}

} // namespace

void* create_file(const wchar_t* name, std::uint32_t access,
                  std::uint32_t share, void* security, std::uint32_t disposition,
                  std::uint32_t flags, void* template_file)
{
	std::optional<rwz::fs::mounts::route> r;
	switch (classify(name, r)) {
	case route_kind::invalid:
		SetLastError(ERROR_INVALID_NAME);
		return INVALID_HANDLE_VALUE;
	case route_kind::host:
		return CreateFileW(name,
		                   access,
		                   share,
		                   static_cast<LPSECURITY_ATTRIBUTES>(security),
		                   disposition,
		                   flags,
		                   static_cast<HANDLE>(template_file));
	case route_kind::archive: break;
	}
	if (!r->backend) {
		SetLastError(ERROR_NOT_SUPPORTED);
		return INVALID_HANDLE_VALUE;
	}
	const auto l = cow::layer_of(*r);

	// Directories: opened with FILE_FLAG_BACKUP_SEMANTICS (as Windows
	// requires), read-only.
	const auto existing = cow::lookup(l);
	if (existing && existing->info.type == rwz::archive::kind::directory) {
		if ((flags & FILE_FLAG_BACKUP_SEMANTICS) == 0 || wants_write(access)) {
			SetLastError(ERROR_ACCESS_DENIED);
			return INVALID_HANDLE_VALUE;
		}
		if (existing->where == cow::origin::overlay) {
			return CreateFileW(existing->host.wstring().c_str(),
			                   access,
			                   share,
			                   static_cast<LPSECURITY_ATTRIBUTES>(security),
			                   OPEN_EXISTING,
			                   flags,
			                   static_cast<HANDLE>(template_file));
		}
		archive_handle h;
		h.directory   = true;
		const auto id = allocate_id();
		std::lock_guard<std::mutex> lock(table_mutex());
		table().emplace(id, std::move(h));
		return to_handle(id);
	}

	if (wants_write(access)) {
		if (r->overlay.empty()) {
			SetLastError(ERROR_WRITE_PROTECT);
			return INVALID_HANDLE_VALUE;
		}
		const bool create    = disposition == CREATE_NEW ||
		                       disposition == CREATE_ALWAYS ||
		                       disposition == OPEN_ALWAYS;
		const bool exclusive = disposition == CREATE_NEW;
		const bool truncate  = disposition == CREATE_ALWAYS ||
		                       disposition == TRUNCATE_EXISTING;
		const auto host = cow::prepare_write(l, create, exclusive, truncate);
		if (!host) {
			SetLastError(win_error_of(host.error()));
			return INVALID_HANDLE_VALUE;
		}
		return CreateFileW(host->wstring().c_str(),
		                   access,
		                   share,
		                   static_cast<LPSECURITY_ATTRIBUTES>(security),
		                   OPEN_EXISTING,
		                   flags,
		                   static_cast<HANDLE>(template_file));
	}

	const auto n = existing;
	if (!n) {
		SetLastError(ERROR_FILE_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	if (n->where == cow::origin::overlay) {
		return CreateFileW(n->host.wstring().c_str(),
		                   access,
		                   share,
		                   static_cast<LPSECURITY_ATTRIBUTES>(security),
		                   OPEN_EXISTING,
		                   flags,
		                   static_cast<HANDLE>(template_file));
	}
	// Archive-only file, opened read-only: a table handle.
	archive_handle h;
	h.backend     = r->backend;
	h.inode       = n->info.inode;
	h.size        = n->info.size;
	const auto id = allocate_id();
	std::lock_guard<std::mutex> lock(table_mutex());
	table().emplace(id, std::move(h));
	return to_handle(id);
}

bool read_file(void* handle, void* buf, std::uint32_t count,
               std::uint32_t* bytes_read, void* overlapped)
{
	const auto id = from_handle(handle);
	std::unique_lock<std::mutex> lock(table_mutex());
	auto it = table().find(id);
	if (it == table().end()) {
		lock.unlock();
		return ReadFile(static_cast<HANDLE>(handle),
		                buf,
		                count,
		                reinterpret_cast<LPDWORD>(bytes_read),
		                static_cast<LPOVERLAPPED>(overlapped)) != 0;
	}
	if (overlapped != nullptr) {
		SetLastError(ERROR_NOT_SUPPORTED);
		return false;
	}
	auto& h = it->second;
	if (h.directory) {
		SetLastError(ERROR_ACCESS_DENIED); // reading a directory handle
		                                   // fails, as on Windows
		return false;
	}
	std::uint64_t got = 0;
	if (h.offset < h.size) {
		const auto want = std::min<std::uint64_t>(count, h.size - h.offset);
		std::error_code ec;
		got = h.backend->read(h.inode,
		                      static_cast<char*>(buf),
		                      static_cast<std::size_t>(want),
		                      h.offset,
		                      ec);
		if (ec) {
			SetLastError(win_error_of(ec));
			return false;
		}
		h.offset += got;
	}
	if (bytes_read != nullptr) {
		*bytes_read = static_cast<std::uint32_t>(got);
	}
	return true;
}

std::uint32_t set_file_pointer(void* handle, std::int32_t distance,
                               std::int32_t* distance_high, std::uint32_t method)
{
	const auto id = from_handle(handle);
	std::lock_guard<std::mutex> lock(table_mutex());
	auto it = table().find(id);
	if (it == table().end()) {
		return SetFilePointer(static_cast<HANDLE>(handle),
		                      distance,
		                      reinterpret_cast<PLONG>(distance_high),
		                      method);
	}
	auto& h = it->second;
	if (h.directory) {
		SetLastError(ERROR_INVALID_FUNCTION);
		return INVALID_SET_FILE_POINTER;
	}
	std::int64_t offset = distance;
	if (distance_high != nullptr) {
		offset = (static_cast<std::int64_t>(*distance_high) << 32) |
		         static_cast<std::uint32_t>(distance);
	}
	std::int64_t base = 0;
	switch (method) {
	case FILE_BEGIN: base = 0; break;
	case FILE_CURRENT: base = static_cast<std::int64_t>(h.offset); break;
	case FILE_END: base = static_cast<std::int64_t>(h.size); break;
	default:
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_SET_FILE_POINTER;
	}
	const auto target = base + offset;
	if (target < 0) {
		SetLastError(ERROR_NEGATIVE_SEEK);
		return INVALID_SET_FILE_POINTER;
	}
	h.offset = static_cast<std::uint64_t>(target);
	if (distance_high != nullptr) {
		*distance_high = static_cast<std::int32_t>(target >> 32);
	}
	return static_cast<std::uint32_t>(target & 0xffffffffu);
}

// ---- directory enumeration on archive paths -------------------------------

namespace {

struct find_state {
	std::vector<WIN32_FIND_DATAW> items;
	std::size_t next{0};
};

std::mutex& find_mutex()
{
	static std::mutex m;
	return m;
}

std::map<std::uintptr_t, find_state>& find_table()
{
	static std::map<std::uintptr_t, find_state> t;
	return t;
}

// Windows matches names case-insensitively, with '*' (any run) and '?' (one
// character).
bool wildcard_match(const std::wstring& pat, const std::wstring& name)
{
	auto lower = [](wchar_t c) {
		return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c;
	};
	std::size_t p = 0, n = 0, star = std::wstring::npos, mark = 0;
	while (n < name.size()) {
		if (p < pat.size() &&
		    (pat[p] == L'?' || lower(pat[p]) == lower(name[n]))) {
			++p;
			++n;
		} else if (p < pat.size() && pat[p] == L'*') {
			star = p++;
			mark = n;
		} else if (star != std::wstring::npos) {
			p = star + 1;
			n = ++mark;
		} else {
			return false;
		}
	}
	while (p < pat.size() && pat[p] == L'*') {
		++p;
	}
	return p == pat.size();
}

std::wstring to_wide(const std::string& utf8)
{
	if (utf8.empty()) {
		return {};
	}
	const int len = MultiByteToWideChar(
	        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	std::wstring out(static_cast<std::size_t>(len), L'\0');
	MultiByteToWideChar(CP_UTF8,
	                    0,
	                    utf8.data(),
	                    static_cast<int>(utf8.size()),
	                    out.data(),
	                    len);
	return out;
}

// Unix seconds to Windows FILETIME (100 ns ticks since 1601-01-01).
FILETIME filetime_of(std::int64_t unix_seconds)
{
	constexpr std::int64_t kEpochDiff = 11644473600LL;
	const auto ticks                  = static_cast<std::uint64_t>(
	        (unix_seconds + kEpochDiff) * 10000000LL);
	FILETIME ft{};
	ft.dwLowDateTime  = static_cast<DWORD>(ticks & 0xffffffffu);
	ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
	return ft;
}

WIN32_FIND_DATAW item_of(const std::string& name, const rwz::archive::entry& info)
{
	WIN32_FIND_DATAW d{};
	const auto w = to_wide(name);
	wcsncpy_s(d.cFileName, w.c_str(), _TRUNCATE);
	const bool is_dir  = info.type == rwz::archive::kind::directory;
	d.dwFileAttributes = is_dir ? FILE_ATTRIBUTE_DIRECTORY
	                            : FILE_ATTRIBUTE_NORMAL;
	if ((info.mode & 0200) == 0) {
		d.dwFileAttributes |= FILE_ATTRIBUTE_READONLY;
	}
	const auto size    = is_dir ? 0ull : info.size;
	d.nFileSizeHigh    = static_cast<DWORD>(size >> 32);
	d.nFileSizeLow     = static_cast<DWORD>(size & 0xffffffffu);
	const auto ft      = filetime_of(info.mtime);
	d.ftCreationTime   = ft;
	d.ftLastAccessTime = ft;
	d.ftLastWriteTime  = ft;
	return d;
}

WIN32_FIND_DATAW dot_item(const wchar_t* name)
{
	WIN32_FIND_DATAW d{};
	wcsncpy_s(d.cFileName, name, _TRUNCATE);
	d.dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
	return d;
}

} // namespace

void* find_first_file(const wchar_t* pattern, void* find_data)
{
	std::optional<rwz::fs::mounts::route> r;
	switch (classify(pattern, r)) {
	case route_kind::invalid:
		SetLastError(ERROR_INVALID_NAME);
		return INVALID_HANDLE_VALUE;
	case route_kind::host:
		return FindFirstFileW(pattern,
		                      static_cast<LPWIN32_FIND_DATAW>(find_data));
	case route_kind::archive: break;
	}
	const std::filesystem::path full(pattern);
	const auto name_pattern = full.filename().wstring();
	const auto dir          = full.parent_path();
	std::error_code ec;
	const auto dir_abs = rwz::fs::resolve(dir, ec);
	const auto dir_route = ec ? std::nullopt : rwz::fs::mounts::find(dir_abs);
	if (!dir_route || !dir_route->backend) {
		SetLastError(ERROR_NOT_SUPPORTED);
		return INVALID_HANDLE_VALUE;
	}
	const auto l  = cow::layer_of(*dir_route);
	const auto dn = cow::lookup(l);
	if (!dn) {
		SetLastError(ERROR_PATH_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	if (dn->info.type != rwz::archive::kind::directory) {
		SetLastError(ERROR_PATH_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	const auto kids = cow::list(l);
	if (!kids) {
		SetLastError(ERROR_PATH_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}

	find_state st;
	for (const auto* dot : {L".", L".."}) {
		if (wildcard_match(name_pattern, dot)) {
			st.items.push_back(dot_item(dot));
		}
	}
	for (const auto& c : *kids) {
		const auto w = to_wide(c.name);
		if (wildcard_match(name_pattern, w)) {
			st.items.push_back(item_of(c.name, c.info));
		}
	}
	if (st.items.empty()) {
		SetLastError(ERROR_FILE_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	*static_cast<WIN32_FIND_DATAW*>(find_data) = st.items[0];
	st.next                                    = 1;
	const auto id                              = allocate_id();
	std::lock_guard<std::mutex> lock(find_mutex());
	find_table().emplace(id, std::move(st));
	return to_handle(id);
}

bool find_next_file(void* find, void* find_data)
{
	const auto id = from_handle(find);
	std::lock_guard<std::mutex> lock(find_mutex());
	auto it = find_table().find(id);
	if (it == find_table().end()) {
		return FindNextFileW(static_cast<HANDLE>(find),
		                     static_cast<LPWIN32_FIND_DATAW>(find_data)) != 0;
	}
	auto& st = it->second;
	if (st.next >= st.items.size()) {
		SetLastError(ERROR_NO_MORE_FILES);
		return false;
	}
	*static_cast<WIN32_FIND_DATAW*>(find_data) = st.items[st.next++];
	return true;
}

bool find_close(void* find)
{
	const auto id = from_handle(find);
	{
		std::lock_guard<std::mutex> lock(find_mutex());
		if (find_table().erase(id) == 1) {
			return true;
		}
	}
	return FindClose(static_cast<HANDLE>(find)) != 0;
}

bool create_directory(const wchar_t* path, void* security)
{
	std::optional<rwz::fs::mounts::route> r;
	switch (classify(path, r)) {
	case route_kind::invalid:
		SetLastError(ERROR_INVALID_NAME);
		return false;
	case route_kind::host:
		return CreateDirectoryW(path,
		                        static_cast<LPSECURITY_ATTRIBUTES>(
		                                security)) != 0;
	case route_kind::archive: break;
	}
	(void)security;
	if (!r->backend) {
		SetLastError(ERROR_NOT_SUPPORTED);
		return false;
	}
	if (r->overlay.empty()) {
		SetLastError(ERROR_WRITE_PROTECT);
		return false;
	}
	if (const auto err = cow::create_dir(cow::layer_of(*r))) {
		SetLastError(win_error_of(err));
		return false;
	}
	return true;
}

bool delete_file(const wchar_t* path)
{
	std::optional<rwz::fs::mounts::route> r;
	switch (classify(path, r)) {
	case route_kind::invalid:
		SetLastError(ERROR_INVALID_NAME);
		return false;
	case route_kind::host: return DeleteFileW(path) != 0;
	case route_kind::archive: break;
	}
	if (!r->backend) {
		SetLastError(ERROR_NOT_SUPPORTED);
		return false;
	}
	if (r->overlay.empty()) {
		SetLastError(ERROR_WRITE_PROTECT);
		return false;
	}
	if (const auto err = cow::remove_file(cow::layer_of(*r))) {
		SetLastError(win_error_of(err));
		return false;
	}
	return true;
}

bool remove_directory(const wchar_t* path)
{
	std::optional<rwz::fs::mounts::route> r;
	switch (classify(path, r)) {
	case route_kind::invalid:
		SetLastError(ERROR_INVALID_NAME);
		return false;
	case route_kind::host: return RemoveDirectoryW(path) != 0;
	case route_kind::archive: break;
	}
	if (!r->backend) {
		SetLastError(ERROR_NOT_SUPPORTED);
		return false;
	}
	if (r->overlay.empty()) {
		SetLastError(ERROR_WRITE_PROTECT);
		return false;
	}
	if (const auto err = cow::remove_dir(cow::layer_of(*r))) {
		SetLastError(win_error_of(err));
		return false;
	}
	return true;
}

bool close_handle(void* handle)
{
	const auto id = from_handle(handle);
	{
		std::lock_guard<std::mutex> lock(table_mutex());
		if (table().erase(id) == 1) {
			return true;
		}
	}
	return CloseHandle(static_cast<HANDLE>(handle)) != 0;
}

} // namespace rwz::win32

#endif // defined(_WIN32)
