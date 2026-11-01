#pragma once

#include <dwarfs/logger.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/reader/filesystem_v2.h>

#include <filesystem>
#include <optional>

#include "rwzfs/archive/backend.hpp"

namespace rwzfs::archive {

// dwarfs-backed archive. Throws if the image cannot be opened.
class dwarfs_backend final : public backend {
 public:
  explicit dwarfs_backend(const std::filesystem::path& image);

  dwarfs_backend(const dwarfs_backend&) = delete;
  dwarfs_backend& operator=(const dwarfs_backend&) = delete;

  [[nodiscard]] std::optional<entry> lookup(std::string_view rel) const override;
  [[nodiscard]] std::size_t read(std::uint64_t inode, char* buf, std::size_t count,
                                 std::uint64_t offset, std::error_code& ec) const override;
  [[nodiscard]] std::optional<std::string> link_target(std::string_view rel) const override;
  [[nodiscard]] std::optional<std::vector<child>> list(std::string_view rel) const override;

 private:
  [[nodiscard]] std::optional<entry> entry_of(const dwarfs::reader::inode_view& iv) const;

  // Declared before fs_: the reader is constructed from these and must not outlive them.
  dwarfs::null_logger lgr_;
  dwarfs::os_access_generic os_;
  dwarfs::reader::filesystem_v2 fs_;
};

}  // namespace rwzfs::archive
