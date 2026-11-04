#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>

namespace rwzfs::fs {

// Converts between std::filesystem timestamps and Unix seconds. Uses the offset between the file
// clock and the system clock, because clock_cast is not available in every standard library
// (Apple's libc++ lacks it). Accuracy is about a microsecond, far finer than the stored seconds.
inline std::chrono::nanoseconds clock_offset() {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()) -
         duration_cast<nanoseconds>(std::filesystem::file_time_type::clock::now().time_since_epoch());
}

inline std::int64_t to_unix_seconds(std::filesystem::file_time_type t) {
  using namespace std::chrono;
  const auto sys = duration_cast<nanoseconds>(t.time_since_epoch()) + clock_offset();
  // Round, not truncate: the two clocks are sampled at slightly different instants, so the
  // round trip can land a few microseconds below a whole second.
  return round<seconds>(sys).count();
}

inline std::filesystem::file_time_type from_unix_seconds(std::int64_t unix_seconds) {
  using namespace std::chrono;
  const auto file = duration_cast<nanoseconds>(seconds(unix_seconds) - clock_offset());
  return std::filesystem::file_time_type(duration_cast<std::filesystem::file_time_type::duration>(file));
}

}  // namespace rwzfs::fs
