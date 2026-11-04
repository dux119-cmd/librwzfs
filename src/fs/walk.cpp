#include "rwzfs/fs/walk.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "rwzfs/fs/archive_path.hpp"

namespace rwzfs::fs {

namespace stdfs = std::filesystem;

namespace detail {

std::vector<std::string> components(std::string_view text) {
  std::vector<std::string> parts;
  std::string current;
  for (const char c : text) {
    if (c == '/') {
      if (!current.empty()) parts.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) parts.push_back(current);
  return parts;
}

stdfs::path absolute_from(const std::vector<std::string>& parts, std::size_t from) {
  const bool drive = !parts.empty() && parts.front().find(':') != std::string::npos;
  std::string text = drive ? "" : "/";
  for (std::size_t i = from; i < parts.size(); ++i) {
    if (i > from) text.push_back('/');
    text += parts[i];
  }
  return stdfs::path(text.empty() ? "/" : text);
}

stdfs::path host_absolute(const std::vector<std::string>& root, const std::vector<std::string>& parts,
                          std::size_t from) {
  const bool drive = !root.empty() && root.front().find(':') != std::string::npos;
  std::string text = drive ? root.front() + "/" : "/";
  for (std::size_t i = from; i < parts.size(); ++i) {
    if (i > from) text.push_back('/');
    text += parts[i];
  }
  return stdfs::path(text);
}

bool has_prefix(const std::vector<std::string>& parts, const std::vector<std::string>& prefix) {
  return parts.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), parts.begin());
}

}  // namespace detail

stdfs::path walk_lexically(const stdfs::path& p) {
  if (!p.is_absolute()) {
    return p;
  }
  const auto tokens = detail::components(p.generic_string());
  const auto root = detail::components(archive_root().generic_string());
  std::vector<std::string> out;
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const auto& token = tokens[i];
    if (token == ".") {
      continue;
    }
    if (token == "..") {
      if (!detail::has_prefix(out, root)) {
        return p;  // host path: leave ".." to the OS so symlinks are followed
      }
      if (out.size() > root.size()) {
        out.pop_back();
        continue;
      }
      return detail::host_absolute(root, tokens, i + 1);  // leaves the archive: the rest is a host path
    }
    out.push_back(token);
  }
  return detail::absolute_from(out, 0);
}

}  // namespace rwzfs::fs
