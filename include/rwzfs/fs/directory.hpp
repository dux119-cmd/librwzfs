#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "rwzfs/archive/backend.hpp"

namespace rwzfs::fs {

// One entry from a listing. Host entries wrap std::filesystem::directory_entry. Archive
// entries carry the backend's metadata.
class directory_entry {
 public:
  directory_entry() = default;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

  [[nodiscard]] std::filesystem::file_status status(std::error_code& ec) const;
  [[nodiscard]] bool is_directory(std::error_code& ec) const;
  [[nodiscard]] bool is_regular_file(std::error_code& ec) const;
  [[nodiscard]] bool is_symlink(std::error_code& ec) const;
  [[nodiscard]] std::uintmax_t file_size(std::error_code& ec) const;

  [[nodiscard]] static directory_entry from_host(const std::filesystem::directory_entry& e);
  [[nodiscard]] static directory_entry from_archive(std::filesystem::path absolute,
                                                    archive::entry info);

 private:
  std::filesystem::path path_;
  std::optional<std::filesystem::directory_entry> host_;
  archive::entry info_{};  // used when host_ is empty
};

namespace detail {

// Position within one archive directory listing. Held by value, so copies advance independently.
struct archive_cursor {
  std::shared_ptr<const archive::backend> backend;
  std::filesystem::path overlay;  // empty when the mount has no overlay
  std::filesystem::path parent_absolute;
  std::string parent_rel;
  std::shared_ptr<const std::vector<archive::child>> children;
  std::size_t index{0};
};

}  // namespace detail

// Lists one directory. Same interface and error reporting as std::filesystem::directory_iterator.
class directory_iterator {
 public:
  using iterator_category = std::input_iterator_tag;
  using value_type = directory_entry;
  using difference_type = std::ptrdiff_t;
  using pointer = const directory_entry*;
  using reference = const directory_entry&;

  directory_iterator() = default;
  directory_iterator(const std::filesystem::path& p, std::error_code& ec);

  [[nodiscard]] reference operator*() const noexcept { return entry_; }
  [[nodiscard]] pointer operator->() const noexcept { return &entry_; }

  directory_iterator& operator++();
  directory_iterator operator++(int);

  friend bool operator==(const directory_iterator& a, const directory_iterator& b);

 private:
  bool valid_{false};
  std::optional<std::filesystem::directory_iterator> host_;
  std::optional<detail::archive_cursor> archive_;
  directory_entry entry_;
};

// Lists a directory tree. Same interface as std::filesystem::recursive_directory_iterator.
class recursive_directory_iterator {
 public:
  using iterator_category = std::input_iterator_tag;
  using value_type = directory_entry;
  using difference_type = std::ptrdiff_t;
  using pointer = const directory_entry*;
  using reference = const directory_entry&;

  recursive_directory_iterator() = default;
  recursive_directory_iterator(const std::filesystem::path& p,
                               std::filesystem::directory_options options, std::error_code& ec);
  recursive_directory_iterator(const std::filesystem::path& p, std::error_code& ec)
      : recursive_directory_iterator(p, std::filesystem::directory_options::none, ec) {}

  [[nodiscard]] reference operator*() const noexcept { return entry_; }
  [[nodiscard]] pointer operator->() const noexcept { return &entry_; }

  recursive_directory_iterator& operator++();
  recursive_directory_iterator operator++(int);

  [[nodiscard]] int depth() const noexcept;
  [[nodiscard]] bool recursion_pending() const noexcept;
  void disable_recursion_pending() noexcept;
  [[nodiscard]] std::filesystem::directory_options options() const noexcept { return options_; }

  friend bool operator==(const recursive_directory_iterator& a,
                         const recursive_directory_iterator& b);

 private:
  void advance_archive();

  bool valid_{false};
  std::filesystem::directory_options options_{std::filesystem::directory_options::none};
  std::optional<std::filesystem::recursive_directory_iterator> host_;
  std::vector<detail::archive_cursor> stack_;  // archive: one frame per open directory
  bool pending_{true};                          // archive: may descend into the current entry
  directory_entry entry_;
};

// Range-for support, so `for (const auto& e : directory_iterator(p, ec))` works as with std.
inline directory_iterator begin(directory_iterator it) noexcept { return it; }
inline directory_iterator end(const directory_iterator&) noexcept { return {}; }
inline recursive_directory_iterator begin(recursive_directory_iterator it) noexcept { return it; }
inline recursive_directory_iterator end(const recursive_directory_iterator&) noexcept { return {}; }

}  // namespace rwzfs::fs
