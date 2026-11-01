#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "rwzfs/archive/backend.hpp"

// In-memory archive for tests that must not depend on dwarfs.
// Modes are POSIX st_mode values written as literals so this header is portable.
class fake_backend final : public rwzfs::archive::backend {
 public:
  static constexpr std::uint32_t kRegularMode = 0100644;
  static constexpr std::uint32_t kDirMode = 040755;
  static constexpr std::uint32_t kSymlinkMode = 0120777;

  fake_backend() { insert("", rwzfs::archive::kind::directory, "", kDirMode); }

  void add_dir(const std::string& rel) { insert(rel, rwzfs::archive::kind::directory, "", kDirMode); }

  void add_file(const std::string& rel, const std::string& content) {
    insert(rel, rwzfs::archive::kind::file, content, kRegularMode);
  }

  void add_symlink(const std::string& rel, const std::string& target) {
    insert(rel, rwzfs::archive::kind::symlink, target, kSymlinkMode);
  }

  [[nodiscard]] std::optional<rwzfs::archive::entry> lookup(std::string_view rel) const override {
    const auto it = index_.find(std::string(rel));
    if (it == index_.end()) return std::nullopt;
    return entry_of(it->second);
  }

  [[nodiscard]] std::size_t read(std::uint64_t inode, char* buf, std::size_t count,
                                 std::uint64_t offset, std::error_code& ec) const override {
    if (inode >= nodes_.size()) {
      ec = std::make_error_code(std::errc::io_error);
      return 0;
    }
    const auto& content = nodes_[inode].content;
    if (offset >= content.size()) return 0;
    const auto n = std::min<std::size_t>(count, content.size() - offset);
    std::memcpy(buf, content.data() + offset, n);
    return n;
  }

  [[nodiscard]] std::optional<std::vector<rwzfs::archive::child>> list(
      std::string_view rel) const override {
    const auto it = index_.find(std::string(rel));
    if (it == index_.end() || nodes_[it->second].type != rwzfs::archive::kind::directory) {
      return std::nullopt;
    }
    std::vector<rwzfs::archive::child> out;
    for (const auto& [key, idx] : index_) {
      if (key.empty() || parent_of(key) != rel) continue;
      out.push_back({last_component(key), entry_of(idx)});
    }
    return out;
  }

 private:
  struct node {
    rwzfs::archive::kind type;
    std::string content;
    std::uint32_t mode;
  };

  static std::string parent_of(const std::string& key) {
    const auto pos = key.rfind('/');
    return pos == std::string::npos ? std::string{} : key.substr(0, pos);
  }

  static std::string last_component(const std::string& key) {
    const auto pos = key.rfind('/');
    return pos == std::string::npos ? key : key.substr(pos + 1);
  }

  void insert(const std::string& rel, rwzfs::archive::kind type, const std::string& content,
              std::uint32_t mode) {
    if (index_.contains(rel)) return;
    if (const auto parent = parent_of(rel); !rel.empty() && !index_.contains(parent)) {
      insert(parent, rwzfs::archive::kind::directory, "", kDirMode);
    }
    index_.emplace(rel, nodes_.size());
    nodes_.push_back(node{type, content, mode});
  }

  [[nodiscard]] rwzfs::archive::entry entry_of(std::size_t idx) const {
    const auto& n = nodes_[idx];
    return rwzfs::archive::entry{n.type, idx, n.content.size(), n.mode};
  }

  std::map<std::string, std::size_t> index_;
  std::vector<node> nodes_;
};
