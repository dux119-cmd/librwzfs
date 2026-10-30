#include "rwzfs/fs/path.hpp"

namespace rwzfs::fs {

// Path operations never touch storage, so they pass through to std::filesystem
// directly. Only IO wrappers can route to an archive.

const path::value_type* c_str(const path& p) noexcept { return p.c_str(); }

std::string string(const path& p) { return p.string(); }

path append(const path& p, std::string_view component) { return p / path(component); }

path make_path(std::string_view text) { return path(text); }

path join(const path& lhs, const path& rhs) { return lhs / rhs; }

path filename(const path& p) { return p.filename(); }

path parent_path(const path& p) { return p.parent_path(); }

path extension(const path& p) { return p.extension(); }

bool is_absolute(const path& p) { return p.is_absolute(); }

path stem(const path& p) { return p.stem(); }

path lexically_relative(const path& p, const path& base) {
  return p.lexically_relative(base);
}

path replace_extension(const path& p, const path& ext) {
  path result = p;
  result.replace_extension(ext);
  return result;
}

path relative_path(const path& p) { return p.relative_path(); }

std::u8string u8string(const path& p) { return p.u8string(); }

}  // namespace rwzfs::fs
