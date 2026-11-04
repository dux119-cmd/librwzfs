#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace rwzfs::archive {

enum class kind {
  file,
  directory,
  symlink,
  other,
};

// Metadata for one archive entry. `mode` is POSIX st_mode (type and permission bits).
struct entry {
  kind type{kind::other};
  std::uint64_t inode{0};
  std::uint64_t size{0};
  std::uint32_t mode{0};
  std::int64_t mtime{0};  // modification time, seconds since the Unix epoch
};

struct child {
  std::string name;
  entry info;
};

// Read-only view of one mounted archive. All paths are archive-relative text ("" = root).
// Implementations must not touch the host filesystem.
class backend {
 public:
  virtual ~backend() = default;

  [[nodiscard]] virtual std::optional<entry> lookup(std::string_view rel) const = 0;

  // Reads up to `count` bytes at `offset`. Returns 0 at or past end of file.
  [[nodiscard]] virtual std::size_t read(std::uint64_t inode, char* buf, std::size_t count,
                                         std::uint64_t offset, std::error_code& ec) const = 0;

  // Target text of a symlink, or nullopt if `rel` is not a readable symlink.
  [[nodiscard]] virtual std::optional<std::string> link_target(std::string_view rel) const = 0;

  // Children of a directory, or nullopt if `rel` is not a directory.
  [[nodiscard]] virtual std::optional<std::vector<child>> list(std::string_view rel) const = 0;
};

}  // namespace rwzfs::archive
