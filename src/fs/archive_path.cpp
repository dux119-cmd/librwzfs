#include "rwzfs/fs/archive_path.hpp"

#include <vector>

namespace rwzfs::fs {

namespace {

constexpr char kSeparator = '/';

bool is_separator(char c) {
#if defined(_WIN32)
  return c == kSeparator || c == '\\';
#else
  return c == kSeparator;
#endif
}

bool starts_with_separator(std::string_view text) {
  return !text.empty() && is_separator(text.front());
}

// Splits on separators and drops empty components, so "a//b/" yields {"a", "b"}.
std::vector<std::string_view> split_components(std::string_view text) {
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (start <= text.size()) {
    std::size_t end = start;
    while (end < text.size() && !is_separator(text[end])) {
      ++end;
    }
    if (end > start) {
      parts.push_back(text.substr(start, end - start));
    }
    start = end + 1;
  }
  return parts;
}

// Resolves "." and ".." lexically. Absolute paths clamp at the root; relative
// paths that climb above the root are an error.
std::expected<std::vector<std::string_view>, archive_path_error> resolve(
    std::string_view text) {
  const bool absolute = starts_with_separator(text);
  std::vector<std::string_view> stack;
  for (const auto part : split_components(text)) {
    if (part == ".") {
      continue;
    }
    if (part == "..") {
      if (!stack.empty()) {
        stack.pop_back();
      } else if (!absolute) {
        return std::unexpected(archive_path_error::escapes_root);
      }
      continue;
    }
    stack.push_back(part);
  }
  return stack;
}

std::string join_components(const std::vector<std::string_view>& parts) {
  std::string result;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      result.push_back(kSeparator);
    }
    result.append(parts[i]);
  }
  return result;
}

}  // namespace

std::expected<std::string, archive_path_error> normalize_archive_path(std::string_view text) {
  auto parts = resolve(text);
  if (!parts) {
    return std::unexpected(parts.error());
  }
  return join_components(*parts);
}

}  // namespace rwzfs::fs
