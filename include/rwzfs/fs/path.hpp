#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace rwzfs::fs {

using path = std::filesystem::path;

// PATH-01 (1319x)
[[nodiscard]] const path::value_type* c_str(const path& p) noexcept;
// PATH-02 (179x)
[[nodiscard]] std::string string(const path& p);
// PATH-03 (65x)
[[nodiscard]] path append(const path& p, std::string_view component);
// PATH-04 (49x) path construction; the `fs::path` type itself is the alias above.
[[nodiscard]] path make_path(std::string_view text);
// PATH-05 (19x)
[[nodiscard]] path join(const path& lhs, const path& rhs);
// PATH-06 (18x)
[[nodiscard]] path filename(const path& p);
// PATH-07 (14x)
[[nodiscard]] path parent_path(const path& p);
// PATH-08 (7x)
[[nodiscard]] path extension(const path& p);
// PATH-09 (6x)
[[nodiscard]] bool is_absolute(const path& p);
// PATH-10 (4x)
[[nodiscard]] path stem(const path& p);
// PATH-11 (2x)
[[nodiscard]] path lexically_relative(const path& p, const path& base);
// PATH-12 (2x)
[[nodiscard]] path replace_extension(const path& p, const path& ext);
// PATH-13 (1x)
[[nodiscard]] path relative_path(const path& p);
// PATH-14 (1x)
[[nodiscard]] std::u8string u8string(const path& p);

}  // namespace rwzfs::fs
