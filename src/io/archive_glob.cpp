#include "rwzfs/io/archive_glob.hpp"

#if !defined(_WIN32)

#include <fnmatch.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "rwzfs/fs/mount_table.hpp"
#include "rwzfs/fs/resolve.hpp"
#include "rwzfs/overlay/cow.hpp"

namespace rwzfs::io::archive_glob {

namespace stdfs = std::filesystem;
namespace cow = rwzfs::overlay::cow;

namespace {

bool has_meta(const std::string& s) { return s.find_first_of("*?[") != std::string::npos; }

std::vector<std::string> split(const std::string& text) {
  std::vector<std::string> out;
  std::string cur;
  for (const char c : text) {
    if (c == '/') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

std::string join(const std::string& base, const std::string& name) {
  return base.empty() ? name : base + "/" + name;
}

// Sets glob_t fields for a list of absolute paths. Storage is malloc'ed so globfree releases it.
int fill(const std::vector<std::string>& paths, glob_t* out) {
  if (paths.empty()) {
    out->gl_pathc = 0;
    out->gl_pathv = nullptr;
    return GLOB_NOMATCH;
  }
  auto** vec = static_cast<char**>(std::malloc((paths.size() + 1) * sizeof(char*)));
  if (vec == nullptr) return GLOB_NOSPACE;
  for (std::size_t i = 0; i < paths.size(); ++i) {
    vec[i] = strdup(paths[i].c_str());
    if (vec[i] == nullptr) {
      for (std::size_t j = 0; j < i; ++j) std::free(vec[j]);
      std::free(vec);
      return GLOB_NOSPACE;
    }
  }
  vec[paths.size()] = nullptr;
  out->gl_pathc = paths.size();
  out->gl_pathv = vec;
  return 0;
}

}  // namespace

int glob(const stdfs::path& absolute_pattern, int flags, glob_t* out) {
  if ((flags & (GLOB_APPEND | GLOB_BRACE | GLOB_TILDE)) != 0) {
    errno = ENOTSUP;
    return GLOB_ABORTED;
  }
  const auto route = fs::mounts::find(absolute_pattern);
  if (!route || !route->backend) {
    errno = ENOTSUP;
    return GLOB_ABORTED;
  }

  // The mountpoint is the absolute path minus the pattern's components.
  const auto components = split(route->rel);
  stdfs::path mount = route->absolute;
  for (std::size_t i = 0; i < components.size(); ++i) mount = mount.parent_path();

  std::vector<std::string> candidates{std::string{}};  // archive-relative, mount-root based
  for (const auto& token : components) {
    std::vector<std::string> next;
    for (const auto& base : candidates) {
      if (has_meta(token)) {
        const auto kids = cow::list(cow::layer{route->backend, base, route->overlay});
        if (!kids) continue;
        for (const auto& c : *kids) {
          if (fnmatch(token.c_str(), c.name.c_str(), FNM_PERIOD) == 0) {
            next.push_back(join(base, c.name));
          }
        }
      } else {
        next.push_back(join(base, token));
      }
    }
    candidates = std::move(next);
  }

  std::vector<std::string> matches;
  for (const auto& rel : candidates) {
    if (cow::lookup(cow::layer{route->backend, rel, route->overlay})) {
      matches.push_back((mount / rel).generic_string());
    }
  }
  std::sort(matches.begin(), matches.end());
  matches.erase(std::unique(matches.begin(), matches.end()), matches.end());

  if (matches.empty() && (flags & GLOB_NOCHECK) != 0) {
    matches.push_back(absolute_pattern.generic_string());
  }
  return fill(matches, out);
}

}  // namespace rwzfs::io::archive_glob

#endif  // !defined(_WIN32)
