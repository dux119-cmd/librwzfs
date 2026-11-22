#!/usr/bin/env python3
"""Vendor rwzfs into an existing CMake C++ project, one replacement per git commit.

The tool walks the project and proposes each change in turn:

  1. Library vendoring (commit): copies the library to <libdir> and links it to the project's
     top-level targets. Asked once, then committed.
  2. Header additions (commit): the rwzfs headers the replacements need, one commit for all files.
  3. Replacements (one commit each): for every call or type it can replace, it shows the change and
     asks [y]es / [n]o / [q]uit. A "yes" applies that one change and commits it, with a generated
     message holding the original text, the replacement and the reason. Bisecting then lands on the
     exact change that introduced a problem.
  4. Sites that need a person (no known replacement, or a call that may be another function): the
     tool shows a suggested replacement signature. Edit the file in your editor, save, then press
     Enter; the file's changes are committed as one commit. 's' skips the site, 'q' quits.
  5. Unused C headers, one commit per removal.

Git is required. Start from a clean tree (or pass --allow-dirty), ideally on a new branch
(--branch). The tool stops and resumes cleanly: committed changes stay, and a rerun finds the
remaining sites.

usage:
  rwzfs_vendor.py <project-root> [--libdir src/libs/rwzfs] [--deps vcpkg|scripts|system] [--no-dwarfs] [--library PATH]
                  [--exclude DIR ...] [--branch NAME] [--allow-dirty] [--dry-run]

Stdlib only. Python 3.9+.
"""
from __future__ import annotations

import argparse
import os
import hashlib
import re
import shutil
import subprocess
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

# std::filesystem functions the library provides as rwz::fs (same names, error_code last).
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

# Every replacement uses the flat namespaces of rwzfs/rwz.h: rwz::posix, rwz::stdio, rwz::win32, rwz::fs.
# rwz::fs functions that also have a throwing form (no error_code argument).
THROWING_FS = {"exists", "create_directories", "is_regular_file", "is_symlink", "equivalent", "absolute",
               "current_path", "resize_file", "directory_iterator"}

RWZ_HEADER = "rwzfs/rwz.h"
INCLUDE_FOR_FAMILY = {"posix": RWZ_HEADER, "stdio": RWZ_HEADER, "win32": RWZ_HEADER, "fs": RWZ_HEADER}
STREAM_TYPES = {"std::ifstream": "rwz::stdio::file_istream",
                "std::ofstream": "rwz::stdio::file_ostream"}

HEADER_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')

SOURCE_EXTS = {".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".h++", ".ipp", ".inl", ".h", ".c"}
CPP_ONLY_EXTS = {".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".h++", ".ipp", ".inl"}
CXX_HINT = re.compile(r"\b(class|namespace|template|std::|constexpr|nullptr|override|noexcept)\b")

# Directories the tool does not scan. The rwzfs library itself is always skipped (its own directory). Other
# vendored code under src/libs (decoders, third-party sources) IS scanned: it does file IO too. Add --exclude
# for any vendored directory that must stay as it is.
DEFAULT_EXCLUDES = {"build", ".git", "external", "third_party"}


# ---- helpers from the whole-file tool
# std::filesystem functions the library provides as rwz::fs (same names, error_code last).
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


def add_includes(text: str, fams: set[str]) -> tuple[str, list[str]]:
    """Add the rwz header once, after the last include, if the file does not have it."""
    headers = sorted({INCLUDE_FOR_FAMILY[f] for f in fams if f in INCLUDE_FOR_FAMILY})
    wanted = [f'#include "{h}"' for h in headers if f'#include "{h}"' not in text]
    if not wanted:
        return text, []
    lines = text.split("\n")
    last_include = None
    for i, l in enumerate(lines):
        if HEADER_RE.match(l):
            last_include = i
    insert_at = (last_include + 1) if last_include is not None else 0
    lines[insert_at:insert_at] = wanted
    return "\n".join(lines), wanted


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


# scripts/deps builds the managed dependencies (dwarfs and its stack) when the vendored library is configured.
# scripts/deps builds the managed dependencies (and dwarfs, from script, against vcpkg). vcpkg-triplets are the
# static triplets for vcpkg. The library's dependency list (vcpkg.json) is merged into the project, not copied.
LIB_COPY_ITEMS = ["include", "src", "CMakeLists.txt", "LICENSE", "scripts/deps", "vcpkg-triplets", "vcpkg-ports", "integrations"]


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


STAMP_NAME = ".vendored-from"


def library_id(lib: Path) -> str:
    """Hash of every file the vendored copy holds. It changes exactly when a refresh would change the copy."""
    h = hashlib.sha256()
    for item in LIB_COPY_ITEMS:
        src = lib / item
        if not src.exists():
            continue
        files = [src] if src.is_file() else sorted(p for p in src.rglob("*") if p.is_file())
        for f in files:
            h.update(str(f.relative_to(lib)).encode() + b"\0" + f.read_bytes() + b"\0")
    return h.hexdigest()[:16]


def library_revision(lib: Path) -> str:
    """The rwzfs git revision the library was taken from, or "" when it is not a git checkout."""
    r = subprocess.run(["git", "-C", str(lib), "rev-parse", "--short", "HEAD"], text=True, capture_output=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def write_stamp(dest: Path, lib: Path) -> None:
    (dest / STAMP_NAME).write_text(f"{library_id(lib)}\n{library_revision(lib)}\n", encoding="utf-8")


def read_stamp(dest: Path) -> tuple[str, str]:
    """(library id, source revision) of the vendored copy, or ("", "") if it has no stamp (older copies)."""
    p = dest / STAMP_NAME
    if not p.is_file():
        return "", ""
    lines = p.read_text(encoding="utf-8").split("\n")
    return lines[0].strip(), (lines[1].strip() if len(lines) > 1 else "")


TARGET_RE = re.compile(r"^\s*(add_executable|add_library)\s*\(\s*([A-Za-z_][\w.-]*)", re.MULTILINE)


BLOCK_BEGIN = "# --- rwzfs (vendored, generated by tools/rwzfs_vendor.py) ---"
BLOCK_END = "# --- end rwzfs ---"
BLOCK_LINK_RE = re.compile(r"^target_link_libraries\((\S+) (PRIVATE|PUBLIC|INTERFACE) rwzfs\)$", re.M)


def merge_vcpkg(root: Path, lib: Path) -> bool:
    """Adds the library's vcpkg dependencies to the project's vcpkg.json, keeping its own entries and order.
    Returns True if the project's file changed. A project without vcpkg.json is left alone."""
    import json
    project = root / "vcpkg.json"
    if not project.is_file():
        return False
    wanted = json.loads((lib / "vcpkg.json").read_text(encoding="utf-8"))["dependencies"]
    data = json.loads(project.read_text(encoding="utf-8"))
    current = data.setdefault("dependencies", [])
    names = {d if isinstance(d, str) else d["name"] for d in current}
    added = [d for d in wanted if d not in names]
    if not added:
        return False
    current.extend(added)
    project.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(f"vcpkg.json: added {len(added)} dependency(ies): {', '.join(added)}")
    return True


def uses_vcpkg(root: Path) -> bool:
    return (root / "vcpkg.json").is_file()


DEPS_CHOICES = ("vcpkg", "scripts", "system")


def resolve_deps(root: Path, requested: str | None) -> str:
    """The dependency source for the project's block: the one requested, else vcpkg when the project has a
    vcpkg.json or VCPKG_ROOT is set, else the managed scripts."""
    if requested:
        return requested
    if uses_vcpkg(root) or os.environ.get("VCPKG_ROOT"):
        return "vcpkg"
    return "scripts"


TARGET_DEF_RE = re.compile(r"add_(?:library|executable)\(\s*([A-Za-z_][\w.+-]*)")


def vendored_target_links(root: Path, libdir: str, excludes: set[str], top_links: set[str]) -> list[str]:
    """The targets in each vendored CMake project (src/libs/<x>/CMakeLists.txt, not rwzfs's own) whose sources the
    rewrite touches, linked to rwzfs. rwzfs's include directory comes with the link, so rwzfs/rwz.h resolves there.
    Guarded with if(TARGET), since a project may not build a given target."""
    own = (root / libdir).resolve()
    names: set[str] = set()
    interface: set[str] = set()  # INTERFACE libraries: CMake accepts only INTERFACE links for them
    for site in scan_project(root, libdir, excludes, "replace")[0]:
        d = (root / site.path).resolve().parent
        while True:
            if root.resolve() not in d.parents and d != root.resolve():
                break
            cml = d / "CMakeLists.txt"
            if cml.is_file() and own not in cml.resolve().parents and cml.resolve().parent != own:
                text = cml.read_text(encoding="utf-8", errors="surrogateescape")
                found = set(TARGET_DEF_RE.findall(text))
                if found:
                    names |= found
                    interface |= set(INTERFACE_LIB_RE.findall(text))
                    break
            if d == root.resolve():
                break
            d = d.parent
    lines: list[str] = []
    for n in sorted(names - top_links):
        vis = "INTERFACE" if n in interface else "PRIVATE"
        lines += [f"if(TARGET {n})", f"  target_link_libraries({n} {vis} rwzfs)", "endif()"]
    return lines


def render_block(libdir: str, with_dwarfs: bool, links: list[str], deps: str, sdl_targets: list[str] | None = None) -> str:
    """The generated block. RWZFS_WITH_DWARFS is forced (the project's choice); RWZFS_DEPS is a default that a
    -DRWZFS_DEPS=... on the command line overrides (no FORCE on it)."""
    dwarfs = "ON" if with_dwarfs else "OFF"
    lines = [
        BLOCK_BEGIN,
        f'set(RWZFS_WITH_DWARFS {dwarfs} CACHE BOOL "Build the dwarfs-backed archive reader" FORCE)',
        'set(RWZFS_BUILD_TESTS OFF CACHE BOOL "" FORCE)',
        'set(RWZFS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)',
        f'set(RWZFS_DEPS "{deps}" CACHE STRING "Dependencies for dwarfs: vcpkg, scripts or system")',
    ]
    if deps == "vcpkg" and with_dwarfs:
        lines += [
            "# vcpkg (the default) installs dwarfs's dependencies. The dwarfs port and static triplets are set above",
            "# project(). Configure with the vcpkg toolchain:",
            "#   -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake   (or set VCPKG_ROOT)",
            "# To build them from source instead:  -DRWZFS_DEPS=scripts",
            "# To use libraries already installed:  -DRWZFS_DEPS=system",
        ]
    lines += [
        f"add_subdirectory({libdir})",
        "# The first configure of a scripts build builds the dependencies: needs bash, git, curl and a C++23",
        "# compiler, and takes a long time. Later configures reuse build-deps/.",
    ]
    lines += links
    for t in sdl_targets or []:
        # The SDL file-open bridge: compiled into each target, since the rewritten C files live in it.
        lines += [
            f"if(TARGET {t})",
            f'  target_sources({t} PRIVATE "${{CMAKE_CURRENT_SOURCE_DIR}}/{libdir}/integrations/sdl/rwz_sdl_io.cpp")',
            "endif()",
        ]
    lines.append(BLOCK_END)
    return "\n".join(lines)


INTERFACE_LIB_RE = re.compile(r"add_library\(\s*(\w+)\s+INTERFACE\b")


def link_visibility(text: str, target: str, current: str = "PRIVATE") -> str:
    """How a target links rwzfs. An INTERFACE library can only link INTERFACE (CMake rejects anything else),
    so it always gets INTERFACE. Any other target keeps its current visibility, PRIVATE by default."""
    if target in set(INTERFACE_LIB_RE.findall(text)):
        return "INTERFACE"
    return current


PRE_BEGIN = "# --- rwzfs: vcpkg overlay (set before project(), where the vcpkg toolchain reads it) ---"
PRE_END = "# --- end rwzfs overlay ---"
PROJECT_RE = re.compile(r"^[ \t]*project[ \t]*\(", re.M | re.I)  # spaces only: \\s would match the newline before it


def render_pre(libdir: str) -> str:
    """Adds the vendored dwarfs port and static triplets to vcpkg's overlay lists. It must sit before project():
    the vcpkg toolchain installs the manifest during project(). The lists are appended to, not replaced, so a
    project's own overlay ports and triplets keep working. Appending twice is a no-op."""
    lines = [PRE_BEGIN]
    for var, sub in (("VCPKG_OVERLAY_PORTS", "vcpkg-ports"), ("VCPKG_OVERLAY_TRIPLETS", "vcpkg-triplets")):
        lines += [
            f'set(_rwz_overlay "${{CMAKE_CURRENT_SOURCE_DIR}}/{libdir}/{sub}")',
            f"list(FIND {var} \"${{_rwz_overlay}}\" _rwz_found)",
            "if(_rwz_found EQUAL -1)",
            f'  list(APPEND {var} "${{_rwz_overlay}}")',
            "endif()",
            "unset(_rwz_overlay)",
            "unset(_rwz_found)",
        ]
    lines.append(PRE_END)
    return "\n".join(lines)


def place_pre(text: str, pre: str | None) -> str:
    """Replaces the overlay block (or removes it when pre is None), placed before the first project() call."""
    if PRE_BEGIN in text and PRE_END in text:
        a = text.index(PRE_BEGIN)
        b = text.index(PRE_END) + len(PRE_END)
        text = text[:a] + text[b:].lstrip("\n")  # the blank lines after the block go with it
    if pre is None:
        return text
    m = PROJECT_RE.search(text)
    if not m:
        return text  # no project() to place it before: patch_cmake reports this
    return text[:m.start()] + pre + "\n\n" + text[m.start():]


def patch_cmake(root: Path, libdir: str, with_dwarfs: bool, report: Report, dry: bool, deps: str = "vcpkg") -> str:
    """Adds the vendored library to the project (the first vendoring)."""
    cml = root / "CMakeLists.txt"
    text = cml.read_text(encoding="utf-8")
    if BLOCK_BEGIN in text:
        report.cmake_notes.append("top-level CMakeLists.txt already contains the rwzfs block; not changed")
        return text
    targets = [m.group(2) for m in TARGET_RE.finditer(text)]
    links = [f"target_link_libraries({t} {link_visibility(text, t)} rwzfs)" for t in targets]
    sdl_targets = targets if needs_sdl_bridge(root, libdir, DEFAULT_EXCLUDES) else None
    links += vendored_target_links(root, libdir, DEFAULT_EXCLUDES, set(targets))
    block = render_block(libdir, with_dwarfs, links, deps, sdl_targets)
    if deps == "vcpkg" and with_dwarfs and not PROJECT_RE.search(text):
        report.cmake_notes.append("no project() call found: add the vcpkg overlay lines (see docs/VENDORING.md) before the first project() call")
    new = text.rstrip("\n") + "\n\n" + block + "\n"
    new = place_pre(new, render_pre(libdir) if deps == "vcpkg" and with_dwarfs else None)
    if not targets:
        report.cmake_notes.append("no add_executable/add_library targets found in the top-level CMakeLists.txt: link rwzfs to your targets (target_link_libraries(<t> PRIVATE rwzfs))")
    if re.search(r"set\s*\(\s*CMAKE_CXX_STANDARD\s+(1[17]|20)\b", text):
        report.cmake_notes.append("CMAKE_CXX_STANDARD is below 23: rwzfs needs C++23 (std::expected). Raise it for the targets that link rwzfs.")
    if not dry:
        cml.write_text(new, encoding="utf-8")
    return new


def regenerate_block(root: Path, libdir: str, with_dwarfs: bool, deps: str = "vcpkg") -> str:
    """The block as it should be now, keeping the project's target_link_libraries lines. Returns the new
    file text, or "" when the project has no block to update."""
    cml = root / "CMakeLists.txt"
    text = cml.read_text(encoding="utf-8")
    if BLOCK_BEGIN not in text or BLOCK_END not in text:
        return ""
    old = text[text.index(BLOCK_BEGIN):text.index(BLOCK_END) + len(BLOCK_END)]
    links = [f"target_link_libraries({m.group(1)} {link_visibility(text, m.group(1), m.group(2))} rwzfs)"
             for m in BLOCK_LINK_RE.finditer(old)]
    top = [m.group(1) for m in BLOCK_LINK_RE.finditer(old)]
    sdl_targets = top if needs_sdl_bridge(root, libdir, DEFAULT_EXCLUDES) else None
    links += vendored_target_links(root, libdir, DEFAULT_EXCLUDES, set(top))
    new = text.replace(old, render_block(libdir, with_dwarfs, links, deps, sdl_targets), 1)
    return place_pre(new, render_pre(libdir) if deps == "vcpkg" and with_dwarfs else None)


def update_block(root: Path, libdir: str, with_dwarfs: bool, deps: str = "vcpkg") -> bool:
    """Brings the project's generated block in line with the library (dwarfs setting, comments) as one commit.
    Asks first. Returns True when a commit was made."""
    new = regenerate_block(root, libdir, with_dwarfs, deps)
    if not new:
        print("no generated rwzfs block found in the top-level CMakeLists.txt; nothing to update.")
        return False
    cml = root / "CMakeLists.txt"
    if new == cml.read_text(encoding="utf-8"):
        print("the generated rwzfs block is up to date.")
        return False
    print(f"the generated block would set RWZFS_WITH_DWARFS {'ON' if with_dwarfs else 'OFF'}.")
    if pick_key("update the generated rwzfs block? [y]es / [n]o") != "y":
        return False
    cml.write_text(new, encoding="utf-8")
    try:
        return commit(root, ["CMakeLists.txt"], "vendor(rwzfs): update the generated CMake block",
                      "Generated by tools/rwzfs_vendor.py --refresh: the rwzfs block in the top-level CMakeLists.txt "
                      f"now matches the library (dwarfs {'on' if with_dwarfs else 'off'}). Only the block changed.\n")
    finally:
        if git(root, "diff", "--quiet", "--cached", "--", "CMakeLists.txt").returncode == 0 and \
                git(root, "diff", "--quiet", "--", "CMakeLists.txt").returncode != 0:
            git(root, "checkout", "--", "CMakeLists.txt")


def scan(root: Path, excludes: set[str], libdir: str) -> list[Path]:
    """Source files to rewrite. The vendored rwzfs copy (libdir) is always skipped, whatever the excludes say:
    rewriting the library's own code would break it. Everything else, including other vendored code under
    src/libs, is scanned unless it is excluded."""
    skip_roots = {(root / e).resolve() for e in excludes | {libdir.rstrip("/")}}
    out = []
    for p in root.rglob("*"):
        if not p.is_file() or p.suffix.lower() not in SOURCE_EXTS:
            continue
        resolved = p.resolve()
        if any(s == resolved or s in resolved.parents for s in skip_roots):
            continue
        out.append(p)
    return sorted(out)


@dataclass
class Site:
    path: str                 # project-relative file
    line: int                 # 1-based
    col: int                  # 0-based column of `old` in the line (valid for the scan that produced it)
    old: str                  # text to replace (or the header line to drop)
    new: str                  # replacement, suggestion, or "" for a removal
    kind: str                 # "rewrite" | "manual" | "drop_header"
    why: str                  # reason, shown and recorded in the commit
    family: str | None = None  # rwzfs header family the replacement needs


@dataclass
class Report:
    cmake_notes: list[str] = field(default_factory=list)
    manual: list[str] = field(default_factory=list)


def site_key(s: Site) -> tuple:
    return (s.path, s.line, s.old, s.kind)


_NON_TYPE_WORDS = {"return", "if", "else", "while", "for", "switch", "case", "throw", "new", "delete",
                   "sizeof", "do", "co_return", "co_await", "not", "and", "or", "using", "typename"}


def _is_parameter_list(args: str) -> bool:
    """True when the argument text reads as a C/C++ parameter list: "", "void", or "type name, ...".
    A call's arguments ("buf, 1, n, fp") name values, not types, so they fail."""
    a = args.strip()
    if a in ("", "void"):
        return True
    for param in a.split(","):
        if not re.match(r"^\s*(?:const\s+)?(?:unsigned\s+|signed\s+|struct\s+)?[A-Za-z_][\w:]*[\s*&]+[A-Za-z_]\w*\s*(?:=.*)?$", param):
            return False
    return True


def _has_return_type(prefix: str, prev: str) -> bool:
    """True when the text before the name ends in a type: on this line ("int read"), or the previous
    line is a type-only fragment ("uint8_t" above "read(...) {"). A statement like "n = fwrite(...)" or
    "return read(...)" is not a declaration."""
    if prefix:
        last = re.search(r"(\w+)$", prefix)
        return bool(re.search(r"[\w*&>]$", prefix)) and (last is None or last.group(1) not in _NON_TYPE_WORDS)
    if not prev or prev.endswith((";", "{", "}", ")", ",", "=")):
        return False
    if "(" in prev or "=" in prev or "#" in prev:
        return False
    last = prev.split()[-1]
    return last not in _NON_TYPE_WORDS and bool(re.match(r"^[A-Za-z_][\w:<>,*& ]*$", prev.strip()))


def local_definitions(rel: str, text: str) -> dict[str, list[str]]:
    """Names of functions defined or declared in this file, with the line. A call of such a name in the
    same file may be the file's own function, not the C one: DOS-emulation code, for example, has its
    own read() and write() on I/O ports. Other files are not consulted.

    A definition is "type name(params) {"; a declaration is "type name(params);". Both need a return type
    before the name and a parameter list inside the parentheses. A call is "name(args);" with no type
    before it, which is why "fwrite(buf, 1, n, fp);" is not a declaration."""
    names = POSIX_FUNCS | STDIO_FUNCS | set(WIN32_FUNCS)
    found: dict[str, list[str]] = {}
    call = re.compile(r"(?<![\w.>:])([A-Za-z_]\w*)\s*\(([^;{}()]*)\)\s*(?:const\s*)?(?:noexcept\s*)?([{;])")
    lines = text.split("\n")
    for lineno, line in enumerate(lines, 1):
        if "#include" in line or "#define" in line:
            continue
        prev = lines[lineno - 2].rstrip() if lineno >= 2 else ""
        for m in call.finditer(line):
            name, args, end = m.group(1), m.group(2), m.group(3)
            if name not in names:
                continue
            if not _has_return_type(line[:m.start()].rstrip(), prev):
                continue
            if end == ";" and not _is_parameter_list(args):
                continue
            found.setdefault(name, []).append(f"{rel}:{lineno}")
    return found


def find_sites(rel: str, text: str, has: set[str], aliases: set[str]) -> tuple[list[Site], int]:
    """Every replacement, manual site and stream type in one file. Returns (sites, silent_count)."""
    local = local_definitions(rel, text)
    sites: list[Site] = []
    silent = 0
    names = POSIX_FUNCS | STDIO_FUNCS | set(WIN32_FUNCS)
    call_re = re.compile(
        r"(?<![\w.>])(?P<q>(?:std)?::)?(?P<name>" + "|".join(sorted(map(re.escape, names), key=len, reverse=True)) + r")\s*\(")
    prefixes = "|".join(sorted(map(re.escape, aliases | FS_NAMESPACES), key=len, reverse=True))
    fs_re = re.compile(
        r"(?<![\w.>:])(?P<ns>(?:" + prefixes + r"))::(?P<name>" +
        "|".join(sorted(map(re.escape, FS_FUNCS), key=len, reverse=True)) + r")\s*\(")
    stream_re = re.compile(r"(?<![\w:])(std::(?:i|o)fstream)\b")
    decl_iter_re = re.compile(
        r"(?<![\w:.])(?P<type>(?:(?:std::filesystem|" + "|".join(sorted(map(re.escape, aliases | FS_NAMESPACES), key=len, reverse=True))
        + r")::)(?P<kind>directory_iterator))(?=\s+\w+\s*[({;=])")
    per_line_old: dict[tuple[int, str], int] = {}
    for lineno, line in enumerate(text.split("\n"), 1):
        if "#include" in line or "#define" in line:
            continue
        mask = _code_mask(line)
        for m in call_re.finditer(line):
            if not mask[m.start("name")]:
                continue
            name = m.group("name")
            q = m.group("q") or ""
            prefix = line[:m.start()]
            if (_is_declaration(prefix) and not q) or (not q and m.start() >= 2 and line[m.start() - 2:m.start()] == "::"):
                silent += 1
                continue
            if name in ALGORITHM_CONFLICT and q == "std::" and not PATHLIKE.match(_first_arg(line, m.end() - 1)):
                silent += 1  # std::remove(first, last) is the container algorithm, not the C call
                continue
            family = "win32" if name in WIN32_FUNCS else "stdio" if name in STDIO_FUNCS else "posix"
            target = f"rwz::win32::{WIN32_FUNCS[name]}" if family == "win32" else f"rwz::{family}::{name}"
            reason = None
            if not q and local and name in local:
                where = ", ".join(local[name][:3]) + (" ..." if len(local[name]) > 3 else "")
                reason = (f"this project defines or declares its own {name}() ({where}); "
                          f"this call may be that function, not the C one")
            if reason is None and family in ("posix", "stdio") and name in AMBIGUOUS and not q:
                headers = POSIX_HEADERS if family == "posix" else STDIO_HEADERS
                if not any(h in headers for h in has):
                    reason = "unqualified and its C header is not included here: this may be another function with the same name"
            if reason is None and name in ALGORITHM_CONFLICT:
                if not PATHLIKE.match(_first_arg(line, m.end() - 1)):
                    reason = "the argument does not look like a path: this may be an algorithm"
            old = m.group(0)
            key = (lineno, old)
            occ = per_line_old.get(key, 0)
            per_line_old[key] = occ + 1
            if reason:
                sites.append(Site(rel, lineno, m.start(), old, f"{target}(", "manual", reason, family))
            else:
                sites.append(Site(rel, lineno, m.start(), old, f"{target}(", "rewrite",
                                  f"{name} is a standard call with an rwzfs wrapper", family))
        for m in fs_re.finditer(line):
            if not mask[m.start("name")]:
                continue
            name = m.group("name")
            last = _last_arg(line, m.end() - 1)
            old = m.group(0)
            if name in THROWING_FS and not (last is not None and (EC_NAME.search(last) or "error_code" in last)):
                # rwz::fs has a throwing form with the same signature: the standard exception comes out
                sites.append(Site(rel, lineno, m.start(), old, f"rwz::fs::{name}(", "rewrite",
                                  "the throwing form of rwz::fs keeps std::filesystem's exception", "fs"))
            elif last is not None and (EC_NAME.search(last) or "error_code" in last):
                sites.append(Site(rel, lineno, m.start(), old, f"rwz::fs::{name}(", "rewrite",
                                  "the call already takes an error_code; the rwzfs form has the same signature",
                                  "fs"))
            else:
                inner = _inner_args(line, m.end() - 1)
                sep = ", " if inner.strip() else ""
                sites.append(Site(rel, lineno, m.start(), old,
                                  f"rwz::fs::{name}({inner}{sep}ec)", "manual",
                                  "std::filesystem throws without an error_code: add an std::error_code and check it",
                                  "fs"))
        # Declarations of the iterator types ("std_fs::directory_iterator it(p);") construct the iterator,
        # which touches the disk, but the call pattern above does not see a name followed by "(".
        for m in decl_iter_re.finditer(line):
            if not mask[m.start()]:
                continue
            sites.append(Site(rel, lineno, m.start(), m.group("type"),
                              f"rwz::fs::{m.group('kind')}", "rewrite",
                              f"the throwing form of rwz::fs keeps std::filesystem's exception",
                              "fs"))
        for m in stream_re.finditer(line):
            if not mask[m.start(1)]:
                continue
            old = m.group(1)
            new = STREAM_TYPES[old]
            sites.append(Site(rel, lineno, m.start(1), old, new, "rewrite",
                              "C++ stream type with an rwzfs stream over the stdio wrappers", "stdio"))
    return sites, silent


SDL_OPEN_RE = re.compile(r"(?<![\w.>:])(?P<name>SDL_IOFromFile|SDL_RWFromFile)\s*\(")
SDL_PUBLIC_HEADER_RE = re.compile(r"(^|/)(SDL[23]|SDL_rwops\.h|SDL_iostream\.h|SDL\.h)$")
BRIDGE_REL = "integrations/sdl/rwz_sdl_io.h"


def sdl_sites(rel: str, text: str) -> list[Site]:
    """SDL's own file opening (SDL_IOFromFile in SDL3, SDL_RWFromFile in SDL2). SDL does its C-library I/O, which
    cannot see a path under the archive root, so these calls go to the bridge (integrations/sdl). Declarations in
    SDL's headers are not rewritten."""
    if SDL_PUBLIC_HEADER_RE.search(rel):
        return []
    out: list[Site] = []
    for lineno, line in enumerate(text.split("\n"), 1):
        if "#include" in line or "#define" in line:
            continue
        mask = _code_mask(line)
        for m in SDL_OPEN_RE.finditer(line):
            if not mask[m.start("name")]:
                continue
            prefix = line[:m.start()].rstrip()
            last = re.search(r"(\w+)$", prefix)
            if re.search(r"[\w*&>]$", prefix) and (last is None or last.group(1) not in _NON_TYPE_WORDS):
                continue  # a declaration: "SDL_RWops *SDL_RWFromFile(...)"
            name = m.group("name")
            out.append(Site(rel, lineno, m.start("name"), name, "rwz_" + name, "rewrite",
                            "SDL opens files with its own C-library I/O, which cannot see the archive; "
                            "the rwzfs bridge opens it through rwz::stdio", "sdl"))
    return out


def bridge_include(rel: str, libdir: str) -> str:
    """The #include line for the bridge header, relative to the file that includes it."""
    target = f"{libdir}/{BRIDGE_REL}"
    return f'#include "{os.path.relpath(target, os.path.dirname(rel) or ".").replace(os.sep, "/")}"'


def needs_sdl_bridge(root: Path, libdir: str, excludes: set[str]) -> bool:
    for path in scan(root, excludes, libdir):
        if SDL_OPEN_RE.search(path.read_text(encoding="utf-8", errors="surrogateescape")):
            return True
    return False


def _inner_args(text: str, open_paren: int) -> str:
    """The text between the parentheses opened at open_paren (empty if unbalanced)."""
    depth = 0
    for j in range(open_paren, min(len(text), open_paren + 400)):
        c = text[j]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[open_paren + 1:j]
    return ""


def drop_sites(rel: str, text: str) -> list[Site]:
    """C headers whose symbols are no longer used in a source file (headers keep their includes)."""
    if Path(rel).suffix.lower() not in {".cpp", ".cc", ".cxx", ".c++"}:
        return []
    lines = text.split("\n")
    code = "\n".join(l for l in lines if not HEADER_RE.match(l))
    out: list[Site] = []
    for lineno, l in enumerate(lines, 1):
        m = HEADER_RE.match(l)
        if not (m and m.group(1) in HEADER_SYMBOLS and "<" in l):
            continue
        sym = HEADER_SYMBOLS[m.group(1)] or HEADER_SYMBOLS["stdio.h"]
        if not re.search(sym, code):
            out.append(Site(rel, lineno, 0, l, "", "drop_header",
                            f"{m.group(1)} is no longer used in this file", None))
    return out


def queue_order(s: Site) -> tuple:
    """File by file; within a file, last line and last column first, so earlier sites keep their place."""
    return (s.path, -s.line, -s.col)


def read_line(root: Path, s: Site) -> str:
    return (root / s.path).read_text(encoding="utf-8", errors="surrogateescape").split("\n")[s.line - 1]


def apply_site(root: Path, s: Site) -> bool:
    """Applies one site. False, and nothing written, if the text there is no longer the scanned text."""
    p = root / s.path
    lines = p.read_text(encoding="utf-8", errors="surrogateescape").split("\n")
    line = lines[s.line - 1]
    if s.kind == "drop_header":
        if line != s.old:
            return False
        del lines[s.line - 1]
    else:
        if line[s.col:s.col + len(s.old)] != s.old:
            return False
        lines[s.line - 1] = line[:s.col] + s.new + line[s.col + len(s.old):]
    p.write_text("\n".join(lines), encoding="utf-8", errors="surrogateescape")
    return True


# ---- git ---------------------------------------------------------------------

def git(root: Path, *args: str, stdin: str | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(root), *args], input=stdin, text=True, capture_output=True)


def require_git(root: Path) -> None:
    r = git(root, "rev-parse", "--is-inside-work-tree")
    if r.returncode != 0 or r.stdout.strip() != "true":
        sys.exit(f"error: {root} is not inside a git work tree. The tool commits each change.")


def dirty(root: Path) -> bool:
    return bool(git(root, "status", "--porcelain").stdout.strip())


def commit(root: Path, paths: list[str], subject: str, body: str) -> bool:
    """Stages exactly `paths` and commits with the generated message. False if nothing changed."""
    if not paths:
        return False
    git(root, "add", "--", *paths)
    if git(root, "diff", "--cached", "--quiet").returncode == 0:
        return False
    message = subject + "\n\n" + body.rstrip() + "\n"
    r = git(root, "commit", "-q", "-F", "-", stdin=message)
    if r.returncode != 0:
        sys.exit(f"error: git commit failed:\n{r.stderr}")
    print(f"  committed: {subject}")
    return True


def site_body(s: Site, before: str, after: str) -> str:
    return (
        "Generated by tools/rwzfs_vendor.py: one replacement per commit, so bisecting can find it.\n\n"
        f"File:        {s.path}:{s.line}\n"
        f"Original:    {s.old}\n"
        f"Replacement: {s.new or '(removed)'}\n"
        f"Reason:      {s.why}\n\n"
        f"Line before: {before.strip()}\n"
        f"Line after:  {after.strip()}\n"
    )


def site_subject(s: Site) -> str:
    if s.kind == "drop_header":
        return f"vendor(rwzfs): drop unused {s.old.strip()} from {s.path}"
    return f"vendor(rwzfs): {s.old.strip()} -> {s.new.strip()} in {s.path}:{s.line}"


# ---- prompts -----------------------------------------------------------------

def show(root: Path, s: Site) -> None:
    line = read_line(root, s)
    print()
    print(f"  {s.path}:{s.line}   {s.kind}")
    print(f"    {line.strip()}")
    if s.kind == "drop_header":
        print(f"    remove: {s.old.strip()}")
    else:
        print(f"    {s.old.strip()}  ->  {s.new.strip()}")
    print(f"    why: {s.why}")


def pick_key(question: str, allow_enter: bool = False) -> str:
    """Asks a question; the letters in [brackets] are the answers. Returns the letter, or "" for Enter."""
    allowed = re.findall(r"\[(\w)\]", question)
    if not allowed:
        raise SystemExit(f"internal error: no answers offered for: {question}")
    for _ in range(50):
        a = input(f"  {question}: ").strip().lower()[:1]
        if a in allowed or (allow_enter and a == ""):
            return a
        print("  choose one of: " + ", ".join(allowed) + (" or Enter" if allow_enter else ""))
    raise SystemExit("too many invalid answers; stopped")


# ---- scanning ----------------------------------------------------------------

def scan_project(root: Path, libdir: str, excludes: set[str], phase: str) -> tuple[list[Site], int]:
    """All sites in the project, in file order. phase is "replace" (calls and types) or "drop" (headers)."""
    inc_cache: dict = {}
    out: list[Site] = []
    silent_total = 0
    for path in scan(root, excludes, libdir):
        rel = str(path.relative_to(root))
        text = path.read_text(encoding="utf-8", errors="surrogateescape")
        if phase == "replace" and path.suffix == ".c":
            out.extend(sdl_sites(rel, text))  # C: only the SDL file-open rule (the rwz C++ names do not compile in C)
            continue
        if path.suffix == ".c" or (path.suffix == ".h" and not CXX_HINT.search(text)):
            continue
        if phase == "replace":
            has = system_includes_reachable(path, root, inc_cache)
            sites, silent = find_sites(rel, text, has, set(FS_ALIAS_DECL.findall(text)))
            silent_total += silent
            out.extend(sites)
            out.extend(sdl_sites(rel, text))
        else:
            out.extend(drop_sites(rel, text))
    return out, silent_total


def refresh_file(root: Path, args, excludes: set[str], queue: list[Site], path: str,
                 skipped: set[tuple]) -> list[Site]:
    """Drops this file's queued sites and rescans the file alone, after its contents changed."""
    p = root / path
    text = p.read_text(encoding="utf-8", errors="surrogateescape")
    if path.endswith(".c"):
        sites = sdl_sites(path, text)
    else:
        has = system_includes_reachable(p, root, {})
        sites, _ = find_sites(path, text, has, set(FS_ALIAS_DECL.findall(text)))
        sites = sites + sdl_sites(path, text)
    rest = [q for q in queue if q.path != path]
    fresh = [q for q in sites if site_key(q) not in skipped]
    return sorted(rest + fresh, key=queue_order)


def header_commit(root: Path, libdir: str, excludes: set[str]) -> None:
    """One commit adding every rwzfs include the replacements will need, before any code change: the C++ wrapper
    headers, and the SDL bridge header in files that open SDL files (C files included)."""
    needed: dict[str, set[str]] = {}
    sdl_files: set[str] = set()
    for path in scan(root, excludes, libdir):
        rel = str(path.relative_to(root))
        text = path.read_text(encoding="utf-8", errors="surrogateescape")
        if sdl_sites(rel, text):
            sdl_files.add(rel)
        if path.suffix == ".c":
            continue  # C: the SDL bridge is the only rwzfs header a C file gets
        if path.suffix == ".h" and not CXX_HINT.search(text):
            continue
        has = system_includes_reachable(path, root, {})
        sites, _ = find_sites(rel, text, has, set(FS_ALIAS_DECL.findall(text)))
        fams = {s.family for s in sites if s.kind == "rewrite" and s.family}
        if fams:
            needed[rel] = fams
    changed: list[str] = []
    for rel in sorted(set(needed) | sdl_files):
        p = root / rel
        text = p.read_text(encoding="utf-8", errors="surrogateescape")
        new = text
        added = False
        if rel in needed:
            new, added_lines = add_includes(new, needed[rel])
            added = added or bool(added_lines)
        if rel in sdl_files:
            line = bridge_include(rel, libdir)
            if line not in new:
                new = _insert_after_last_include(new, line)
                added = True
        if added:
            p.write_text(new, encoding="utf-8", errors="surrogateescape")
            changed.append(rel)
    if changed:
        body = "Generated by tools/rwzfs_vendor.py: the rwzfs headers used by the replacements that follow.\n\n" + \
               "\n".join(f"  {rel}" for rel in changed) + "\n"
        commit(root, changed, "vendor(rwzfs): include the rwzfs headers the replacements need", body)


def _insert_after_last_include(text: str, line: str) -> str:
    """Adds one #include line after the file's last #include (or at the top)."""
    lines = text.split("\n")
    last = None
    for i, l in enumerate(lines):
        if HEADER_RE.match(l):
            last = i
    lines.insert(0 if last is None else last + 1, line)
    return "\n".join(lines)


RWZ_HEADER_LINE = '#include "rwzfs/rwz.h"'
RWZ_HEADER_RE = re.compile(r'#\s*include\s*[<"]rwzfs/rwz\.h[>"]')


def ensure_rwz_includes(root: Path, libdir: str, excludes: set[str]) -> None:
    """Every C++ file that uses rwz:: includes rwzfs/rwz.h, as one commit made after the rewrites. The include is
    added here, not with the earlier header commit, so a file rewritten later still gets it."""
    changed: list[str] = []
    for path in scan(root, excludes, libdir):
        if path.suffix.lower() == ".c":
            continue  # C files get the SDL bridge include, not the C++ header
        rel = str(path.relative_to(root))
        text = path.read_text(encoding="utf-8", errors="surrogateescape")
        if "rwz::" not in text or RWZ_HEADER_RE.search(text):
            continue
        path.write_text(_insert_after_last_include(text, RWZ_HEADER_LINE), encoding="utf-8", errors="surrogateescape")
        changed.append(rel)
    if changed:
        body = ("Generated by tools/rwzfs_vendor.py: these files use rwz:: names and now include rwzfs/rwz.h.\n\n"
                + "\n".join(f"  {rel}" for rel in changed) + "\n")
        commit(root, changed, "vendor(rwzfs): include rwzfs/rwz.h where the rwz:: names are used", body)


def revert_path(root: Path, libdir: str) -> None:
    """Restores the vendored directory (and the project's vcpkg.json) to the last commit."""
    git(root, "reset", "-q", "--", libdir, "vcpkg.json")
    git(root, "checkout", "--", libdir, "vcpkg.json")
    git(root, "clean", "-fdq", "--", libdir)


def refresh_library(root: Path, libdir: str, lib: Path, allow_dirty: bool, with_dwarfs: bool = True,
                    deps: str = "vcpkg") -> int:
    """Replaces the vendored copy with the current library, as one commit. Nothing else changes.

    The up-to-date test compares the files in the copy, not the stamp: a stamp is only text, and the
    files are what the project builds. Any failure or refusal after the copy reverts the directory."""
    dest = root / libdir
    if not dest.is_dir():
        sys.exit(f"error: {libdir} is not vendored yet. Run the tool without --refresh first.")
    _, old_rev = read_stamp(dest)
    new_id = library_id(lib)
    if library_id(dest) == new_id:
        print(f"{libdir} is up to date (library {new_id}{', revision ' + old_rev if old_rev else ''}).")
        if merge_vcpkg(root, lib):
            commit(root, ["vcpkg.json"], "vendor(rwzfs): add the library's dependencies to vcpkg.json",
                   "Generated by tools/rwzfs_vendor.py --refresh: dependencies the library now needs.\n")
        update_block(root, libdir, with_dwarfs, deps)  # the library can be current while the block is stale
        return 0
    if git(root, "status", "--porcelain", "--", libdir).stdout.strip() and not allow_dirty:
        sys.exit(f"error: {libdir} has uncommitted changes. Commit or revert them, or pass --allow-dirty.")
    print(f"library {library_id(dest)} -> {new_id}  (from {lib})")
    committed = False
    try:
        copy_library(lib, dest, False)
        write_stamp(dest, lib)
        manifest = merge_vcpkg(root, lib)
        git(root, "add", "-A", "--", libdir)
        if manifest:
            git(root, "add", "--", "vcpkg.json")
        stat = git(root, "diff", "--cached", "--stat").stdout.strip()
        if not stat:
            print("no change in the copy; nothing to commit.")
            update_block(root, libdir, with_dwarfs, deps)
            return 0
        print(stat)
        if pick_key("commit this library update? [y]es / [n]o (discard)") != "y":
            print("discarded.")
            return 0
        rev = library_revision(lib)
        body = ("Generated by tools/rwzfs_vendor.py --refresh: the vendored library, replaced by the current one.\n\n"
                f"Library:  {new_id}" + (f"\nRevision: {rev}" if rev else "") + "\n\n" + stat + "\n")
        committed = commit(root, [libdir] + (["vcpkg.json"] if manifest else []),
                           f"vendor(rwzfs): update the vendored library to {new_id}", body)
        update_block(root, libdir, with_dwarfs, deps)
        return 0
    finally:
        if not committed:
            revert_path(root, libdir)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("project_root", type=Path)
    ap.add_argument("--libdir", default="src/libs/rwzfs")
    ap.add_argument("--library", type=Path, default=Path(__file__).resolve().parent.parent)
    ap.add_argument("--deps", choices=DEPS_CHOICES, default=None,
                    help="dependency source the project's default uses: vcpkg (default if found), scripts, or system")
    ap.add_argument("--no-dwarfs", action="store_true",
                    help="vendor without the dwarfs archive reader (no managed dependencies)")
    ap.add_argument("--exclude", nargs="*", default=[])
    ap.add_argument("--branch", default=None, help="create and switch to this branch first")
    ap.add_argument("--allow-dirty", action="store_true")
    ap.add_argument("--dry-run", action="store_true", help="list the sites, change nothing")
    ap.add_argument("--refresh", action="store_true",
                    help="replace the vendored library with the current one (one commit); changes nothing else")
    args = ap.parse_args(argv)

    root = args.project_root.resolve()
    if not (root / "CMakeLists.txt").is_file():
        sys.exit(f"error: {root} has no CMakeLists.txt")
    lib = args.library.resolve()
    if not (lib / "include" / "rwzfs").is_dir():
        sys.exit(f"error: {lib} does not look like the rwzfs source tree")
    require_git(root)
    if args.refresh:
        return refresh_library(root, args.libdir, lib, args.allow_dirty, with_dwarfs=not args.no_dwarfs,
                               deps=resolve_deps(root, args.deps))
    excludes = DEFAULT_EXCLUDES | set(args.exclude)

    replace_sites, silent = scan_project(root, args.libdir, excludes, "replace")
    print(f"sites: {sum(s.kind == 'rewrite' for s in replace_sites)} replacements, "
          f"{sum(s.kind == 'manual' for s in replace_sites)} need a person; "
          f"{silent} calls correctly left alone")
    if args.dry_run:
        for s in replace_sites:
            show(root, s)
        return 0

    if not args.allow_dirty and dirty(root):
        sys.exit("error: the work tree has uncommitted changes. Commit or stash them, or pass --allow-dirty.")
    if args.branch:
        r = git(root, "checkout", "-b", args.branch)
        if r.returncode != 0:
            sys.exit(f"error: {r.stderr}")
        print(f"on new branch {args.branch}")

    # 1. library vendoring
    vendored = (root / args.libdir).is_dir()
    if not vendored:
        if pick_key("vendor the library and link it to the top-level targets? [y]es / [n]o").lower() != "y":
            return 0
        copy_library(lib, root / args.libdir, False)
        write_stamp(root / args.libdir, lib)
        patch_cmake(root, args.libdir, not args.no_dwarfs, Report(), False, resolve_deps(root, args.deps))
        manifest = merge_vcpkg(root, lib)
        commit(root, [args.libdir, "CMakeLists.txt"] + (["vcpkg.json"] if manifest else []),
               f"vendor(rwzfs): add the library at {args.libdir} and link it",
               "Generated by tools/rwzfs_vendor.py. Copies the library and adds its subdirectory and\n"
               "target_link_libraries() lines to the top-level CMakeLists.txt.\n")

    # 2. header additions
    header_commit(root, args.libdir, excludes)

    # 3-4. replacements, one per commit. The sites are scanned once, into a queue. Within a file the
    # queue runs bottom-up, so a change never moves a site that is still waiting. A manual edit rescans
    # only the file it touched.
    queue = sorted(scan_project(root, args.libdir, excludes, "replace")[0], key=queue_order)
    skipped: set[tuple] = set()
    quit_now = False
    while queue and not quit_now:
        s = queue.pop(0)
        if site_key(s) in skipped:
            continue
        show(root, s)
        if s.kind == "rewrite":
            choice = pick_key("apply this replacement? [y]es / [n]o (skip) / [q]uit")
            if choice == "q":
                quit_now = True
            elif choice == "y":
                before = read_line(root, s)
                if not apply_site(root, s):
                    print("  the line changed since it was queued; rescanning this file")
                    queue = refresh_file(root, args, excludes, queue, s.path, skipped)
                    continue
                after = read_line(root, s)
                commit(root, [s.path], site_subject(s), site_body(s, before, after))
            else:
                skipped.add(site_key(s))
        else:
            print("  needs a person: edit it in your editor and save the file.")
            choice = pick_key("[enter] edited, commit the file's changes / [s] skip this site / [q] quit",
                              allow_enter=True)
            if choice == "q":
                quit_now = True
            else:
                skipped.add(site_key(s))
                if choice == "" and git(root, "diff", "--quiet", "--", s.path).returncode != 0:
                    commit(root, [s.path], f"vendor(rwzfs): manual change at {s.path}:{s.line}",
                           site_body(s, s.old, "(edited by hand)"))
                    queue = refresh_file(root, args, excludes, queue, s.path, skipped)
                elif choice == "":
                    print("  no change in that file; the site is skipped")

    if quit_now:
        print("stopped. Rerun to continue with the remaining sites.")
        return 0

    ensure_rwz_includes(root, args.libdir, excludes)

    # 5. unused C headers: one scan, then one commit per removal (bottom-up within each file)
    drops = sorted(scan_project(root, args.libdir, excludes, "drop")[0], key=queue_order)
    while drops:
        s = drops.pop(0)
        if site_key(s) in skipped:
            continue
        show(root, s)
        choice = pick_key("remove this include? [y]es / [n]o (skip) / [q]uit")
        if choice == "q":
            break
        if choice == "y":
            if not apply_site(root, s):
                print("  the line changed since it was queued; skipped")
                continue
            commit(root, [s.path], site_subject(s), site_body(s, s.old, ""))
        else:
            skipped.add(site_key(s))

    print("done.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
