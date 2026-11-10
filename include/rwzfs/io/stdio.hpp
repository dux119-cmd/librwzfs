#pragma once

#include <sys/types.h>

#include <cstdio>
#include <fstream>
#include <string>

namespace rwzfs::io::stdio {

// STDIO-01 (295x)
std::size_t fread(void* buf, std::size_t size, std::size_t count, std::FILE* stream);
// STDIO-02 (68x). Archive files open read-only. Write modes fail with EROFS.
// An archive stream owns a private in-memory copy of the file. Close it with fclose below.
std::FILE* fopen(const char* path, const char* mode);
// STDIO-10 (new). Writes to a stream. Archive streams are read-only, so writes to them fail.
std::size_t fwrite(const void* buf, std::size_t size, std::size_t count, std::FILE* stream);
// STDIO-11 (new). Repositions any stream (host or archive).
int fseek(std::FILE* stream, long offset, int whence);
#if !defined(_WIN32)
// STDIO-12 / STDIO-13 (new). Repositioning and position with off_t offsets (POSIX).
int fseeko(std::FILE* stream, off_t offset, int whence);
off_t ftello(std::FILE* stream);
#endif
// STDIO-09 (new). Closes any stream. Required for archive streams, which own a buffer.
int fclose(std::FILE* stream);
// STDIO-03 (80x)
long ftell(std::FILE* stream);
// STDIO-04 (12x)
int fflush(std::FILE* stream);
// STDIO-05 (21x) C `remove`, distinct from `fs::remove`.
int remove(const char* path);
// STDIO-06 (1x) C `rename`, distinct from `fs::rename`.
int rename(const char* from, const char* to);
// STDIO-07 (2x)
std::ifstream input_file_stream(const std::string& path);
// STDIO-08 (2x)
std::ofstream output_file_stream(const std::string& path);

}  // namespace rwzfs::io::stdio
