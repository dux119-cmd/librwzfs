#!/usr/bin/env python3
"""Patch dwarfs' timestamp parser for Apple's libc++.

dwarfs v0.15.5 calls std::chrono::from_stream in parse_time_point (used by
mkdwarfs --set-time). Apple's libc++ does not provide it. This replaces that one call
with std::get_time + timegm on Apple platforms only. Nothing else changes.

usage: patch_dwarfs_apple.py <dwarfs-source-dir>
"""
import pathlib
import sys

OLD = """#else
    std::chrono::from_stream(iss, fmt, tp);
#endif"""

NEW = """#elif defined(__APPLE__)
    // Apple's libc++ lacks std::chrono::from_stream (patched by rwzfs CI).
    std::tm tm{};
    iss >> std::get_time(&tm, fmt);
    if (!iss.fail()) {
      tp = std::chrono::system_clock::from_time_t(timegm(&tm));
    }
#else
    std::chrono::from_stream(iss, fmt, tp);
#endif"""

INCLUDE_OLD = "#include <chrono>"
INCLUDE_NEW = "#include <chrono>\n#include <ctime>\n#include <iomanip>"


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    path = pathlib.Path(sys.argv[1]) / "src" / "util.cpp"
    text = path.read_text()
    if NEW in text:
        print("already patched")
        return 0
    if text.count(OLD) != 1:
        print(f"expected exactly one match in {path}; found {text.count(OLD)}")
        return 1
    text = text.replace(OLD, NEW, 1)
    if INCLUDE_OLD in text and "<iomanip>" not in text:
        text = text.replace(INCLUDE_OLD, INCLUDE_NEW, 1)
    path.write_text(text)
    print(f"patched {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
