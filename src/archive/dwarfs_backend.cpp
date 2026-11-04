#include "rwzfs/archive/dwarfs_backend.hpp"

#include <cstdint>

namespace rwzfs::archive {

namespace {

kind kind_of(const dwarfs::reader::inode_view& iv) {
  if (iv.is_directory()) return kind::directory;
  if (iv.is_regular_file()) return kind::file;
  if (iv.is_symlink()) return kind::symlink;
  return kind::other;
}

}  // namespace

dwarfs_backend::dwarfs_backend(const std::filesystem::path& image)
    : fs_(lgr_, os_, image) {}

std::optional<entry> dwarfs_backend::entry_of(const dwarfs::reader::inode_view& iv) const {
  std::error_code ec;
  const auto st = fs_.getattr(iv, ec);
  if (ec) {
    return std::nullopt;
  }
  entry e{kind_of(iv), iv.inode_num(), static_cast<std::uint64_t>(st.size()),
          static_cast<std::uint32_t>(st.mode())};
  e.mtime = static_cast<std::int64_t>(st.mtime());
  return e;
}

std::optional<entry> dwarfs_backend::lookup(std::string_view rel) const {
  const auto dir_entry = fs_.find(rel);
  if (!dir_entry) {
    return std::nullopt;
  }
  return entry_of(dir_entry->inode());
}

std::size_t dwarfs_backend::read(std::uint64_t inode, char* buf, std::size_t count,
                                 std::uint64_t offset, std::error_code& ec) const {
  return fs_.read(static_cast<std::uint32_t>(inode), buf, count,
                  static_cast<std::int64_t>(offset), ec);
}

std::optional<std::string> dwarfs_backend::link_target(std::string_view rel) const {
  const auto dir_entry = fs_.find(rel);
  if (!dir_entry) {
    return std::nullopt;
  }
  const auto iv = dir_entry->inode();
  if (!iv.is_symlink()) {
    return std::nullopt;
  }
  std::error_code ec;
  auto target = fs_.readlink(iv, ec);
  if (ec) {
    return std::nullopt;
  }
  return target;
}

std::optional<std::vector<child>> dwarfs_backend::list(std::string_view rel) const {
  const auto dir_entry = fs_.find(rel);
  if (!dir_entry) {
    return std::nullopt;
  }
  const auto iv = dir_entry->inode();
  if (!iv.is_directory()) {
    return std::nullopt;
  }
  const auto dir = fs_.opendir(iv);
  if (!dir) {
    return std::nullopt;
  }
  std::vector<child> out;
  for (const auto& e : *dir) {
    const auto info = entry_of(e.inode());
    if (!info) {
      return std::nullopt;
    }
    out.push_back(child{e.name(), *info});
  }
  return out;
}

}  // namespace rwzfs::archive
