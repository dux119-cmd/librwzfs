#!/usr/bin/env python3
"""Vendor rwzfs into an existing CMake C++ project and switch its IO calls to the library.

What it does, for a project root:
  1. Copies the library (include/, src/, CMakeLists.txt) to <root>/<libdir> (default
     src/libs/rwzfs).
  2. Adds to the top-level CMakeLists.txt: a block that adds the library subdirectory, and
     target_link_libraries(<target> PRIVATE rwzfs) for each add_executable/add_library target
     declared in that file.
  3. Rewrites C++ sources and headers: calls to the standard and POSIX IO functions become the
     library's wrappers (rwzfs::io::posix::, rwzfs::io::stdio::, rwzfs::io::win32::). It adds the
     matching '#include "rwzfs/..."' lines and drops C headers whose symbols are no longer used.
  4. Writes a report of everything it did and everything it skipped or that needs a person.

It is deliberately conservative. A call is left alone (and listed in the report) when it is a
member call, a declaration, inside a string or comment, or when the name is ambiguous and the file
does not include the C header that provides it. Nothing is rewritten in C sources (the library is
C++ only); they are listed.

usage:
  rwzfs_vendor.py <project-root> [--libdir src/libs/rwzfs] [--with-dwarfs] [--dry-run]
                  [--report FILE] [--exclude DIR ...]

Stdlib only. Python 3.9+.
"""
from __future__ import annotations

import argparse
import difflib
import re
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path

# ---- library mapping -------------------------------------------------------

POSIX_FUNCS = {
    "open", "close", "read", "write", "lseek", "fsync", "stat", "ioctl", "unlink", "mmap",
    "realpath", "opendir", "readdir", "closedir", "mkdir", "rmdir", "truncate", "access",
    "getcwd", "glob", "chdir", "chmod", "utimensat",
}
STDIO_FUNCS = {"fopen", "fread", "fwrite", "fseek", "fseeko", "ftello", "ftell", "fflush", "fclose", "remove", "rename"}
WIN32_FUNCS = {
    "CreateFileW": "create_file", "ReadFile": "read_file", "SetFilePointer": "set_file_pointer",
    "FindFirstFileW": "find_first_file", "CreateDirectoryW": "create_directory",
    "CloseHandle": "close_handle",
}

# Functions whose names are common in other code. Unqualified calls are rewritten only when the
# file includes the C header that declares them (the family's headers below).
AMBIGUOUS = {"open", "close", "read", "write", "remove", "rename", "stat", "access", "glob",
             "mkdir", "chmod", "truncate", "ioctl", "unlink", "rmdir", "readdir", "opendir",
             "closedir", "lseek", "fsync", "realpath", "getcwd", "chdir", "mmap", "utimensat",
             "fopen", "fread", "fwrite", "fseek", "fseeko", "ftello", "ftell", "fflush", "fclose"}
POSIX_HEADERS = {"fcntl.h", "unistd.h", "sys/stat.h", "dirent.h", "sys/mman.h", "sys/ioctl.h",
                 "glob.h", "sys/time.h", "time.h", "sys/types.h", "stdlib.h", "limits.h"}
STDIO_HEADERS = {"stdio.h", "cstdio"}

# Remove-style names that are also std algorithms: std::remove/std::rename are never rewritten.
ALGORITHM_CONFLICT = {"remove", "rename"}
PATHLIKE = re.compile(r'^\s*(?:"|[A-Za-z_][\w.]*(?:path|file|name|dir|str)\b)', re.IGNORECASE)

KEYWORDS_BEFORE_CALL = {"return", "else", "case", "throw", "co_return", "do", "new", "delete",
                        "sizeof", "not", "and", "or", "if", "while", "for", "switch", "co_await",
                        "typename", "using", "decltype"}

# C headers that the rewritten calls make unnecessary, and the symbols that keep them.
HEADER_SYMBOLS = {
    "stdio.h": r"\b(printf|fprintf|sprintf|snprintf|vsnprintf|vprintf|vfprintf|puts|fputs|fputc|putc|"
               r"putchar|getc|getchar|fgetc|fgets|gets|scanf|sscanf|fscanf|perror|fwrite|fseek|"
               r"rewind|feof|ferror|clearerr|setvbuf|tmpfile|BUFSIZ|EOF|FILE|stdin|stdout|stderr|"
               r"SEEK_SET|SEEK_CUR|SEEK_END|fpos_t|ungetc|setbuf|fgetpos|fsetpos)\b",
    "cstdio": None,  # same as stdio.h
    "fcntl.h": r"(?<![:\w.>])(O_[A-Z]+|F_OK|R_OK|W_OK|X_OK|F_GETFL|F_SETFL|F_GETFD|F_SETFD|FD_CLOEXEC|creat|fcntl)\b",
    "unistd.h": r"(?<![:\w.>])(close|read|write|lseek|access|getcwd|chdir|unlink|rmdir|fsync|sleep|"
                r"usleep|getpid|STDIN_FILENO|STDOUT_FILENO|STDERR_FILENO|F_OK|R_OK|W_OK|X_OK|"
                r"SEEK_SET|SEEK_CUR|SEEK_END|sysconf|getuid|geteuid|isatty)\b",
    "sys/stat.h": r"(\bstruct\s+stat\b|\bS_I[A-Z]+\b|(?<![:\w.>])(mkdir|chmod|fstat|lstat|stat)\b\s*\()",
    "dirent.h": r"(\bDIR\b|\bdirent\b|(?<![:\w.>])(opendir|readdir|closedir)\b\s*\()",
    "sys/types.h": r"\b(mode_t|off_t|ssize_t|pid_t|uid_t|gid_t|dev_t|ino_t|nlink_t)\b",
    "sys/mman.h": r"(?<![:\w.>])(mmap|munmap|mprotect|msync)\b\s*\(|\b(PROT_[A-Z]+|MAP_[A-Z]+)\b",
    "sys/ioctl.h": r"(?<![:\w.>])ioctl\b\s*\(|\bFIO[A-Z]+\b",
    "glob.h": r"\bglob_t\b|(?<![:\w.>])(glob|globfree)\b\s*\(|\bGLOB_[A-Z]+\b",
    "fstream": r"\b(i|o)?fstream\b",
}
HEADER_SYMBOLS["cstdio"] = HEADER_SYMBOLS["stdio.h"]

# std::filesystem functions the library provides as rwzfs::fs (same names, error_code last).
FS_FUNCS = {
    "exists", "status", "copy", "create_directories", "is_regular_file", "is_directory",
    "absolute", "canonical", "directory_iterator", "is_empty", "permissions", "is_symlink",
    "equivalent", "current_path", "read_symlink", "create_directory", "space",
    "recursive_directory_iterator", "proximate", "last_write_time", "remove_all", "resize_file",
    "remove", "rename",
}
# Namespace names that always mean std::filesystem in the project (shorthand), plus any alias
# declared in the file as `namespace X = std::filesystem;`.
FS_NAMESPACES = {"std::filesystem", "std_fs"}
FS_ALIAS_DECL = re.compile(r"namespace\s+(\w+)\s*=\s*(?:std::filesystem|std_fs)\s*;")
EC_NAME = re.compile(r"(?i)(^|_)(ec|err|error|code)(_|$)|ec$")

INCLUDE_FOR_FAMILY = {"posix": "rwzfs/io/posix.hpp", "stdio": "rwzfs/io/stdio.hpp",
                      "win32": "rwzfs/io/win32.hpp", "fs": "rwzfs/fs/filesystem.hpp"}

HEADER_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')

SOURCE_EXTS = {".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".h++", ".ipp", ".inl", ".h", ".c"}
CPP_ONLY_EXTS = {".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".h++", ".ipp", ".inl"}
CXX_HINT = re.compile(r"\b(class|namespace|template|std::|constexpr|nullptr|override|noexcept)\b")

DEFAULT_EXCLUDES = {"build", ".git", "external", "third_party", "src/libs"}


# ---- data ------------------------------------------------------------------

@dataclass
class Report:
    rewritten: dict[str, int] = field(default_factory=dict)        # function -> count
    files_changed: list[str] = field(default_factory=list)
    headers_added: dict[str, list[str]] = field(default_factory=dict)
    headers_dropped: dict[str, list[str]] = field(default_factory=dict)
    skipped: list[tuple[str, int, str, str]] = field(default_factory=list)  # file, line, name, why
    not_calls: dict[str, int] = field(default_factory=dict)                # counted, not listed
    manual: list[str] = field(default_factory=list)
    cmake_notes: list[str] = field(default_factory=list)


# ---- source rewriting ------------------------------------------------------

def _code_mask(line: str) -> list[bool]:
    """True for each character that is real code (not inside a string, char literal or comment)."""
    mask = [True] * len(line)
    i, n = 0, len(line)
    in_str: str | None = None
    while i < n:
        c = line[i]
        if in_str:
            mask[i] = False
            if c == "\\" and i + 1 < n:
                mask[i + 1] = False
                i += 2
                continue
            if c == in_str:
                in_str = None
        else:
            if c in ('"', "'"):
                in_str = c
                mask[i] = False
            elif c == "/" and i + 1 < n and line[i + 1] == "/":
                for j in range(i, n):
                    mask[j] = False
                break
        i += 1
    return mask


def _is_declaration(prefix: str) -> bool:
    """True when the text before a call looks like a declaration or definition of that name."""
    stripped = prefix.rstrip()
    if not stripped:
        return False
    if stripped.endswith(("*", "&")):
        return True
    m = re.search(r"([A-Za-z_]\w*)$", stripped)
    # A word before the name (a type, or a class/struct scope) makes it a declaration.
    if m and m.group(1) not in KEYWORDS_BEFORE_CALL:
        return True
    return False


def _first_arg(text: str, open_paren: int) -> str:
    depth, start = 0, open_paren + 1
    for j in range(open_paren, min(len(text), open_paren + 200)):
        c = text[j]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[start:j].split(",")[0]
        elif c == "," and depth == 1:
            return text[start:j]
    return text[start:start + 60]


def rewrite_calls(text: str, path: str, report: Report, has: dict[str, bool]) -> str:
    names = POSIX_FUNCS | STDIO_FUNCS | set(WIN32_FUNCS)
    pattern = re.compile(
        r"(?<![\w.>])(?P<q>(?:std)?::)?(?P<name>" + "|".join(sorted(map(re.escape, names), key=len, reverse=True)) + r")\s*\(")
    out_lines = []
    lineno = 0
    for line in text.split("\n"):
        lineno += 1
        if "#include" in line or "#define" in line:
            out_lines.append(line)
            continue
        mask = _code_mask(line)
        result, pos = [], 0
        for m in pattern.finditer(line):
            if not mask[m.start("name")]:
                continue
            name = m.group("name")
            q = m.group("q") or ""
            prefix = line[:m.start()]
            if not q and m.start() >= 2 and line[m.start() - 2:m.start()] == "::":
                # qualified by another namespace or class (std_fs::remove, Foo::read): not a C call
                report.not_calls["qualified by another namespace or class"] = report.not_calls.get("qualified by another namespace or class", 0) + 1
                continue
            if _is_declaration(prefix) and not q:
                report.not_calls["declaration or definition"] = report.not_calls.get("declaration or definition", 0) + 1
                continue
            if name in ALGORITHM_CONFLICT and q == "std::":
                report.not_calls["std algorithm (std::%s)" % name] = report.not_calls.get("std algorithm (std::%s)" % name, 0) + 1
                continue
            family = None
            if name in WIN32_FUNCS:
                family = "win32"
            elif name in STDIO_FUNCS:
                family = "stdio"
            elif name in POSIX_FUNCS:
                family = "posix"
            if family in ("posix", "stdio") and name in AMBIGUOUS and not q:
                headers = POSIX_HEADERS if family == "posix" else STDIO_HEADERS
                if not (has["any:" + family] or any(h in headers for h in has["includes"])):
                    report.skipped.append((path, lineno, name, "unqualified, C header not included"))
                    continue
            if name in ALGORITHM_CONFLICT:
                arg = _first_arg(line, m.end() - 1)
                if not PATHLIKE.match(arg):
                    report.skipped.append((path, lineno, name, "argument is not clearly a path"))
                    continue
            if q == "::" and name in POSIX_FUNCS | STDIO_FUNCS:
                pass  # ::open(...) etc. is the global C function: rewrite
            if family == "win32":
                target = f"rwzfs::io::win32::{WIN32_FUNCS[name]}"
            else:
                target = f"rwzfs::io::{family}::{name}"
            result.append(line[pos:m.start()])
            result.append(target + "(")
            pos = m.end()
            report.rewritten[name] = report.rewritten.get(name, 0) + 1
            has.setdefault("used:" + family, True)
        result.append(line[pos:])
        out_lines.append("".join(result))
    return "\n".join(out_lines)


def _last_arg(text: str, open_paren: int) -> str | None:
    depth = 0
    last_comma = open_paren
    for j in range(open_paren, min(len(text), open_paren + 400)):
        c = text[j]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                return text[last_comma + 1:j].strip()
        elif c == "," and depth == 1:
            last_comma = j
    return None


def rewrite_fs_calls(text: str, path: str, report: Report, aliases: set[str]) -> str:
    """std::filesystem::NAME(args, ec) -> rwzfs::fs::NAME(args, ec). Calls without an error_code
    argument are reported: the standard form throws, and needs a person to port."""
    prefixes = "|".join(sorted(map(re.escape, aliases | FS_NAMESPACES), key=len, reverse=True))
    pattern = re.compile(
        r"(?<![\w.>:])(?P<ns>(?:" + prefixes + r"))::(?P<name>" +
        "|".join(sorted(map(re.escape, FS_FUNCS), key=len, reverse=True)) + r")\s*\(")
    out_lines = []
    for lineno, line in enumerate(text.split("\n"), start=1):
        if "#include" in line or "namespace" in line and "=" in line:
            out_lines.append(line)
            continue
        mask = _code_mask(line)
        result, pos = [], 0
        for m in pattern.finditer(line):
            if not mask[m.start("name")]:
                continue
            name = m.group("name")
            last = _last_arg(line, m.end() - 1)
            if last is None or not (EC_NAME.search(last) or "error_code" in last):
                report.skipped.append((path, lineno, "fs::" + name,
                                       "std::filesystem form without an error_code: throws; port by hand"))
                continue
            result.append(line[pos:m.start()])
            result.append(f"rwzfs::fs::{name}(")
            pos = m.end()
            key = "fs::" + name
            report.rewritten[key] = report.rewritten.get(key, 0) + 1
        result.append(line[pos:])
        out_lines.append("".join(result))
    return "\n".join(out_lines)


def families_used(text: str) -> set[str]:
    fams = set()
    if "rwzfs::io::posix::" in text:
        fams.add("posix")
    if "rwzfs::io::stdio::" in text:
        fams.add("stdio")
    if "rwzfs::io::win32::" in text:
        fams.add("win32")
    if "rwzfs::fs::" in text:
        fams.add("fs")
    return fams


def add_includes(text: str, fams: set[str]) -> tuple[str, list[str]]:
    lines = text.split("\n")
    wanted = []
    for fam in ("posix", "stdio", "win32", "fs"):
        if fam in fams:
            inc = f'#include "{INCLUDE_FOR_FAMILY[fam]}"'
            if inc not in text:
                wanted.append(inc)
    if not wanted:
        return text, []
    last_include = None
    for i, l in enumerate(lines):
        if HEADER_RE.match(l):
            last_include = i
    insert_at = (last_include + 1) if last_include is not None else 0
    lines[insert_at:insert_at] = wanted
    return "\n".join(lines), wanted


def drop_includes(text: str, path: str, report: Report) -> str:
    lines = text.split("\n")
    code_wo_includes = "\n".join(l for l in lines if not HEADER_RE.match(l))
    dropped = []
    keep = []
    for l in lines:
        m = HEADER_RE.match(l)
        if m and m.group(1) in HEADER_SYMBOLS and "<" in l:
            header = m.group(1)
            sym = HEADER_SYMBOLS[header]
            if sym is None:
                sym = HEADER_SYMBOLS["stdio.h"]
            if not re.search(sym, code_wo_includes):
                dropped.append(header)
                continue
        keep.append(l)
    if dropped:
        report.headers_dropped.setdefault(path, []).extend(dropped)
    return "\n".join(keep)


def manual_findings(text: str, path: str, report: Report) -> None:
    """One note per file and kind. Filesystem calls that were already listed as skipped are not
    repeated here."""
    skipped_lines = {ln for f, ln, _n, _w in report.skipped if f == path}
    stream_seen = fs_seen = False
    for i, line in enumerate(text.split("\n"), start=1):
        if "#include" in line:
            continue
        if not stream_seen and re.search(r"\bstd::(i|o)?fstream\b", line):
            report.manual.append(f"{path}: C++ file streams (std::ifstream/ofstream) are not rewritten. Use rwzfs::io::stdio::input_file_stream/output_file_stream after review: an archive path gives a failed stream.")
            stream_seen = True
        if not fs_seen and i not in skipped_lines and re.search(r"\b(std::filesystem|std_fs)::(?!path\b)\w+", line) and "rwzfs::fs::" not in line:
            report.manual.append(f"{path}: std::filesystem calls that are not in rwzfs::fs or are not rewritten. Check by hand.")
            fs_seen = True


QUOTED_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)
SYS_INCLUDE = re.compile(r'^\s*#\s*include\s*<([^>]+)>', re.MULTILINE)


def system_includes_reachable(path: Path, root: Path, cache: dict[Path, set[str]],
                              stack: tuple[Path, ...] = ()) -> set[str]:
    """System headers included by `path` directly or through project headers (quoted includes that
    resolve inside the project). A C header counts as available if any reachable file includes it:
    the file compiles with it, even if it does not include it itself."""
    path = path.resolve()
    if path in cache:
        return cache[path]
    if path in stack or len(stack) > 12 or not path.is_file():
        return set()
    try:
        text = path.read_text(encoding="utf-8", errors="surrogateescape")
    except OSError:
        return set()
    found = set(SYS_INCLUDE.findall(text))
    for inc in QUOTED_INCLUDE.findall(text):
        for base in (path.parent, root, root / "src", root / "include"):
            candidate = (base / inc).resolve()
            if candidate.is_file() and root in candidate.parents:
                found |= system_includes_reachable(candidate, root, cache, stack + (path,))
                break
    cache[path] = found
    return found


def process_source(path: Path, rel: str, report: Report, dry: bool,
                   root: Path | None = None, inc_cache: dict | None = None) -> str | None:
    text = path.read_text(encoding="utf-8", errors="surrogateescape")
    if path.suffix == ".c" or (path.suffix == ".h" and not CXX_HINT.search(text)):
        report.manual.append(f"{rel}: C source: the library is C++ only. Convert the file to C++ or add a C++ shim.")
        return None
    if root is not None and inc_cache is not None:
        includes = system_includes_reachable(path, root, inc_cache)
    else:
        includes = {m.group(1) for l in text.split("\n") if (m := HEADER_RE.match(l))}
    has = {"includes": includes}
    for fam, headers in (("posix", POSIX_HEADERS), ("stdio", STDIO_HEADERS)):
        has["any:" + fam] = False
    aliases = set(FS_ALIAS_DECL.findall(text))
    new = rewrite_calls(text, rel, report, has)
    new = rewrite_fs_calls(new, rel, report, aliases)
    fams = families_used(new) - families_used(text)
    if new != text:
        new, added = add_includes(new, families_used(new))
        if added:
            report.headers_added.setdefault(rel, []).extend(added)
    # Only .cpp-style sources drop includes. A header's includes reach the files that include it, so
    # they are never dropped there.
    if path.suffix.lower() in {".cpp", ".cc", ".cxx", ".c++"}:
        new = drop_includes(new, rel, report)
    manual_findings(new, rel, report)
    if new == text:
        return None
    if not dry:
        path.write_text(new, encoding="utf-8", errors="surrogateescape")
    report.files_changed.append(rel)
    if dry:
        diff = "".join(difflib.unified_diff(text.splitlines(True), new.splitlines(True), rel, rel))
        return diff
    return None


# ---- library copy and CMake ------------------------------------------------

LIB_COPY_ITEMS = ["include", "src", "CMakeLists.txt", "LICENSE"]


def copy_library(lib_src: Path, dest: Path, dry: bool) -> None:
    if dry:
        return
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    for item in LIB_COPY_ITEMS:
        src = lib_src / item
        if not src.exists():
            continue
        if src.is_dir():
            shutil.copytree(src, dest / item)
        else:
            shutil.copy2(src, dest / item)


TARGET_RE = re.compile(r"^\s*(add_executable|add_library)\s*\(\s*([A-Za-z_][\w.-]*)", re.MULTILINE)


def patch_cmake(root: Path, libdir: str, with_dwarfs: bool, report: Report, dry: bool) -> str:
    cml = root / "CMakeLists.txt"
    text = cml.read_text(encoding="utf-8")
    if "rwzfs (vendored" in text:
        report.cmake_notes.append("top-level CMakeLists.txt already contains the rwzfs block; not changed")
        return text
    targets = [m.group(2) for m in TARGET_RE.finditer(text)]
    dwarfs = "ON" if with_dwarfs else "OFF"
    block = [
        "",
        "# --- rwzfs (vendored, generated by tools/rwzfs_vendor.py) ---",
        f'set(RWZFS_WITH_DWARFS {dwarfs} CACHE BOOL "Build the dwarfs-backed archive reader")',
        'set(RWZFS_BUILD_TESTS OFF CACHE BOOL "" FORCE)',
        'set(RWZFS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)',
        f"add_subdirectory({libdir})",
    ]
    for t in targets:
        block.append(f"target_link_libraries({t} PRIVATE rwzfs)")
    block.append("# --- end rwzfs ---")
    new = text.rstrip("\n") + "\n" + "\n".join(block) + "\n"
    if not targets:
        report.cmake_notes.append("no add_executable/add_library targets found in the top-level CMakeLists.txt: link rwzfs to your targets (target_link_libraries(<t> PRIVATE rwzfs))")
    if re.search(r"set\s*\(\s*CMAKE_CXX_STANDARD\s+(1[17]|20)\b", text):
        report.cmake_notes.append("CMAKE_CXX_STANDARD is below 23: rwzfs needs C++23 (std::expected). Raise it for the targets that link rwzfs.")
    if not dry:
        cml.write_text(new, encoding="utf-8")
    return new


# ---- main ------------------------------------------------------------------

def scan(root: Path, excludes: set[str], libdir: str) -> list[Path]:
    skip_roots = {(root / e).resolve() for e in excludes | {libdir}}
    out = []
    for p in root.rglob("*"):
        if not p.is_file() or p.suffix.lower() not in SOURCE_EXTS:
            continue
        resolved = p.resolve()
        if any(s == resolved or s in resolved.parents for s in skip_roots):
            continue
        out.append(p)
    return sorted(out)


def write_report(report: Report, path: Path | None, root: Path) -> str:
    lines = ["rwzfs vendoring report", "=" * 22, ""]
    total = sum(report.rewritten.values())
    lines.append(f"files changed: {len(report.files_changed)}    calls rewritten: {total}")
    lines.append("")
    lines.append("rewritten calls:")
    for name, n in sorted(report.rewritten.items(), key=lambda kv: -kv[1]):
        lines.append(f"  {name:20s} {n}")
    lines.append("")
    lines.append("includes added:")
    for f, incs in sorted(report.headers_added.items()):
        lines.append(f"  {f}: {', '.join(incs)}")
    lines.append("")
    lines.append("includes dropped (no remaining use):")
    for f, hs in sorted(report.headers_dropped.items()):
        lines.append(f"  {f}: {', '.join(hs)}")
    lines.append("")
    lines.append("not calls to wrap (counted, correctly left alone):")
    for why, n in sorted(report.not_calls.items(), key=lambda kv: -kv[1]):
        lines.append(f"  {why}: {n}")
    lines.append("")
    by_reason: dict[str, list[tuple[str, int, str]]] = {}
    for f, ln, name, why in report.skipped:
        by_reason.setdefault(why, []).append((f, ln, name))
    lines.append(f"skipped call sites ({len(report.skipped)}), grouped by reason:")
    for why, items in sorted(by_reason.items(), key=lambda kv: -len(kv[1])):
        lines.append(f"  [{why}] {len(items)}")
        for f, ln, name in items[:200]:
            lines.append(f"    {f}:{ln}: {name}")
        if len(items) > 200:
            lines.append(f"    ... {len(items) - 200} more")
    lines.append("")
    lines.append("needs a person:")
    for m in report.manual[:500]:
        lines.append(f"  {m}")
    lines.append("")
    lines.append("CMake:")
    for n in report.cmake_notes:
        lines.append(f"  {n}")
    text = "\n".join(lines) + "\n"
    if path:
        path.write_text(text, encoding="utf-8")
    return text


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("project_root", type=Path)
    ap.add_argument("--libdir", default="src/libs/rwzfs", help="where to vendor the library, relative to the project root")
    ap.add_argument("--library", type=Path, default=Path(__file__).resolve().parent.parent,
                    help="path to the rwzfs source tree to copy (default: this repository)")
    ap.add_argument("--with-dwarfs", action="store_true", help="build the dwarfs reader (needs dwarfs installed)")
    ap.add_argument("--dry-run", action="store_true", help="show what would change; write nothing")
    ap.add_argument("--report", type=Path, default=None, help="write the report to this file")
    ap.add_argument("--exclude", nargs="*", default=[], help="extra directories (relative to the root) to skip")
    ap.add_argument("--diff", action="store_true", help="with --dry-run, print a diff for every changed file")
    args = ap.parse_args(argv)

    root = args.project_root.resolve()
    if not (root / "CMakeLists.txt").is_file():
        print(f"error: {root} has no CMakeLists.txt", file=sys.stderr)
        return 2
    lib = args.library.resolve()
    if not (lib / "include" / "rwzfs").is_dir() or not (lib / "src").is_dir():
        print(f"error: {lib} does not look like the rwzfs source tree", file=sys.stderr)
        return 2

    report = Report()
    dest = root / args.libdir
    print(f"library: {lib}\nvendor:  {dest}\nproject: {root}")
    if args.dry_run:
        print("dry run: nothing will be written")

    copy_library(lib, dest, args.dry_run)
    report.cmake_notes.append(f"library vendored at {args.libdir} (dwarfs reader: {'on' if args.with_dwarfs else 'off'})")

    patch_cmake(root, args.libdir, args.with_dwarfs, report, args.dry_run)

    excludes = DEFAULT_EXCLUDES | set(args.exclude)
    diffs = []
    inc_cache: dict = {}
    for path in scan(root, excludes, args.libdir):
        rel = str(path.relative_to(root))
        d = process_source(path, rel, report, args.dry_run, root, inc_cache)
        if d:
            diffs.append(d)
    if args.dry_run and args.diff:
        print("".join(diffs))

    text = write_report(report, args.report, root)
    print(text)
    print("done." if not args.dry_run else "dry run complete.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
