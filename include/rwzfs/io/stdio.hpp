#pragma once

#include <cstdio>
#include <fstream>
#include <string>

namespace rwzfs::io::stdio {

// STDIO-01 (295x)
[[nodiscard]] std::size_t fread(void* buf, std::size_t size, std::size_t count, std::FILE* stream);
// STDIO-02 (68x)
[[nodiscard]] std::FILE* fopen(const char* path, const char* mode);
// STDIO-03 (80x)
[[nodiscard]] long ftell(std::FILE* stream);
// STDIO-04 (12x)
[[nodiscard]] int fflush(std::FILE* stream);
// STDIO-05 (21x) C `remove`, distinct from `fs::remove`.
[[nodiscard]] int remove(const char* path);
// STDIO-06 (1x) C `rename`, distinct from `fs::rename`.
[[nodiscard]] int rename(const char* from, const char* to);
// STDIO-07 (2x)
[[nodiscard]] std::ifstream input_file_stream(const std::string& path);
// STDIO-08 (2x)
[[nodiscard]] std::ofstream output_file_stream(const std::string& path);

}  // namespace rwzfs::io::stdio
