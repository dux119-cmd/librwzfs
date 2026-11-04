#include "rwzfs/overlay/flatten.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "rwzfs/overlay/host_meta.hpp"

namespace rwzfs::overlay {

namespace stdfs = std::filesystem;

namespace {

constexpr std::uint64_t kChunk = 64 * 1024;

std::error_code write_archive_file(const cow::layer& l, const archive::entry& info,
                                   const stdfs::path& dst) {
  std::ofstream out(dst, std::ios::binary | std::ios::trunc);
  if (!out) return std::make_error_code(std::errc::io_error);
  std::vector<char> buf(static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, info.size)) + 1);
  std::uint64_t offset = 0;
  std::error_code ec;
  while (offset < info.size) {
    const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, info.size - offset));
    const auto got = l.backend->read(info.inode, buf.data(), want, offset, ec);
    if (ec) return ec;
    if (got == 0) return std::make_error_code(std::errc::io_error);
    out.write(buf.data(), static_cast<std::streamsize>(got));
    if (!out) return std::make_error_code(std::errc::io_error);
    offset += got;
  }
  return {};
}

// Modes and times are applied when the host supports them. On Windows they are best effort:
// content and structure are always written, and metadata failures do not abort a flatten.
std::error_code apply_metadata(const stdfs::path& p, const archive::entry& info) {
  const auto mode_err = host_meta::set_mode(p, info.mode & 07777);
  const auto time_err = host_meta::set_mtime(p, info.mtime);
#if defined(_WIN32)
  (void)mode_err;
  (void)time_err;
  return {};
#else
  if (mode_err) return mode_err;
  return time_err;
#endif
}

std::error_code flatten_dir(const cow::layer& dir, const stdfs::path& host) {
  const auto kids = cow::list(dir);
  if (!kids) return std::make_error_code(std::errc::not_a_directory);
  for (const auto& c : *kids) {
    cow::layer child = dir;
    child.rel = dir.rel.empty() ? c.name : dir.rel + "/" + c.name;
    const auto node = cow::lookup(child);
    if (!node) continue;
    const auto dst = host / c.name;
    std::error_code ec;
    switch (node->info.type) {
      case archive::kind::directory:
        stdfs::create_directory(dst, ec);
        if (ec) return ec;
        if (auto err = flatten_dir(child, dst)) return err;
        if (auto err = apply_metadata(dst, node->info)) return err;
        break;
      case archive::kind::file:
        if (node->where == cow::origin::overlay) {
          stdfs::copy_file(node->host, dst, stdfs::copy_options::overwrite_existing, ec);
          if (ec) return ec;
        } else if (auto err = write_archive_file(child, node->info, dst)) {
          return err;
        }
        if (auto err = apply_metadata(dst, node->info)) return err;
        break;
      case archive::kind::symlink: {
        const auto target = child.backend->link_target(child.rel);
        if (!target) return std::make_error_code(std::errc::io_error);
        stdfs::create_symlink(*target, dst, ec);
        if (ec) return ec;
        break;
      }
      case archive::kind::other:
        break;  // devices, fifos and sockets are not part of an archive
    }
  }
  return {};
}

}  // namespace

std::error_code flatten(const cow::layer& root, const stdfs::path& host_dir) {
  if (auto err = flatten_dir(root, host_dir)) return err;
  return {};
}

}  // namespace rwzfs::overlay
