#pragma once

#include <sys/types.h>

#include <cstdio>
#include <filesystem>
#include <istream>
#include <memory>
#include <ostream>
#include <streambuf>
#include <string>

namespace rwz::stdio {

// STDIO-01 (295x)
std::size_t fread(void* buf, std::size_t size, std::size_t count, std::FILE* stream);
// STDIO-02 (68x). Archive files open read-only. Write modes fail with EROFS.
// An archive stream owns a private in-memory copy of the file. Close it with
// fclose below.
std::FILE* fopen(const char* path, const char* mode);
// STDIO-10 (new). Writes to a stream. Archive streams are read-only, so writes
// to them fail.
std::size_t fwrite(const void* buf, std::size_t size, std::size_t count,
                   std::FILE* stream);
// STDIO-11 (new). Repositions any stream (host or archive).
int fseek(std::FILE* stream, long offset, int whence);
#if !defined(_WIN32)
// STDIO-12 / STDIO-13 (new). Repositioning and position with off_t offsets
// (POSIX).
int fseeko(std::FILE* stream, off_t offset, int whence);
off_t ftello(std::FILE* stream);
#endif
// STDIO-09 (new). Closes any stream. Required for archive streams, which own a
// buffer.
int fclose(std::FILE* stream);
// STDIO-03 (80x)
long ftell(std::FILE* stream);
// STDIO-04 (12x)
int fflush(std::FILE* stream);
// STDIO-05 (21x) C `remove`, distinct from `rwz::fs::remove`.
int remove(const char* path);
// STDIO-06 (1x) C `rename`, distinct from `rwz::fs::rename`.
int rename(const char* from, const char* to);
// C++ streams over the stdio wrappers above, so archive and overlay paths work
// as with fopen (no archive mounted: the stream is in the failed state).
// Movable, not copyable.
class file_istream : public std::istream {
public:
	explicit file_istream(const std::string& path);
	explicit file_istream(const char* path);
	explicit file_istream(const std::filesystem::path& path);
	file_istream(file_istream&& other) noexcept;
	file_istream& operator=(file_istream&&) = delete;
	~file_istream() override;
	[[nodiscard]] bool is_open() const noexcept;

private:
	class buf;
	std::shared_ptr<buf> buf_ = {}; // shared_ptr: the buffer type is
	                                // private to stdio.cpp
};

class file_ostream : public std::ostream {
public:
	explicit file_ostream(const std::string& path);
	explicit file_ostream(const char* path);
	explicit file_ostream(const std::filesystem::path& path);
	file_ostream(file_ostream&& other) noexcept;
	file_ostream& operator=(file_ostream&&) = delete;
	~file_ostream() override;
	[[nodiscard]] bool is_open() const noexcept;

private:
	class buf;
	std::shared_ptr<buf> buf_ = {}; // shared_ptr: the buffer type is
	                                // private to stdio.cpp
};

// STDIO-07 (2x). Opens for reading ("rb").
file_istream input_file_stream(const std::string& path);
// STDIO-08 (2x). Opens for writing ("wb"), truncating: archive files copy up
// into the overlay first.
file_ostream output_file_stream(const std::string& path);

} // namespace rwz::stdio
