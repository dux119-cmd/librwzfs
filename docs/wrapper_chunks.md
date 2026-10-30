# rwzfs wrapper chunks

Each wrapper has an ID and call count from the call list. Status: `declared` = header only, `implemented` = definition written, `tested` = covered by a passing test.

## Path (PATH) — include/rwzfs/fs/path.hpp
| ID | Call | Calls | Status |
|---|---|---|---|
| PATH-01 | fs::path::c_str | 1319 | implemented, tested |
| PATH-02 | fs::path::string | 179 | implemented, tested |
| PATH-03 | fs::path::append | 65 | implemented, tested |
| PATH-04 | fs::path (construction) | 49 | implemented, tested |
| PATH-05 | fs::path::operator/ | 19 | implemented, tested |
| PATH-06 | fs::path::filename | 18 | implemented, tested |
| PATH-07 | fs::path::parent_path | 14 | implemented, tested |
| PATH-08 | fs::path::extension | 7 | implemented, tested |
| PATH-09 | fs::path::is_absolute | 6 | implemented, tested |
| PATH-10 | fs::path::stem | 4 | implemented, tested |
| PATH-11 | fs::path::lexically_relative | 2 | implemented, tested |
| PATH-12 | fs::path::replace_extension | 2 | implemented, tested |
| PATH-13 | fs::path::relative_path | 1 | implemented, tested |
| PATH-14 | fs::path::u8string | 1 | implemented, tested |

## Archive paths (PATH) — include/rwzfs/fs/archive_path.hpp
| ID | Call | Calls | Status |
|---|---|---|---|
| PATH-15 | normalize_archive_path (new, see docs/archive_paths.md) | n/a | implemented, tested |

## Filesystem (FS) — include/rwzfs/fs/filesystem.hpp
Routing: a path at or under a registered mountpoint (lexical check, see `include/rwzfs/fs/mount_table.hpp`) is an archive path. Until archive lookups exist, archive paths return `std::errc::not_supported`. All other paths, including relative paths, pass through to `std::filesystem`.

| ID | Call | Calls | Status |
|---|---|---|---|
| FS-01 | fs::exists | 16 | implemented, tested (passthrough + routing) |
| FS-02 | fs::status | 21 | implemented, tested (passthrough + routing) |
| FS-03 | fs::copy | 16 | implemented, tested (passthrough + routing) |
| FS-04 | fs::create_directories | 15 | implemented, tested (passthrough + routing) |
| FS-05 | fs::is_regular_file | 11 | implemented, tested (passthrough + routing) |
| FS-06 | fs::is_directory | 10 | implemented, tested (passthrough + routing) |
| FS-07 | fs::absolute | 10 | implemented, tested (passthrough + routing) |
| FS-08 | fs::canonical | 9 | implemented, tested (passthrough + routing) |
| FS-09 | fs::directory_iterator | 7 | implemented, tested (passthrough + routing) |
| FS-10 | fs::is_empty | 6 | implemented, tested (passthrough + routing) |
| FS-11 | fs::permissions | 5 | implemented, tested (passthrough + routing) |
| FS-12 | fs::is_symlink | 4 | implemented, tested (passthrough + routing) |
| FS-13 | fs::equivalent | 4 | implemented, tested (passthrough + routing) |
| FS-14 | fs::current_path | 2 | implemented, tested (passthrough + routing) |
| FS-15 | fs::read_symlink | 2 | implemented, tested (passthrough + routing) |
| FS-16 | fs::create_directory | 2 | implemented, tested (passthrough + routing) |
| FS-17 | fs::space | 2 | implemented, tested (passthrough + routing) |
| FS-18 | fs::recursive_directory_iterator | 1 | implemented, tested (passthrough + routing) |
| FS-19 | fs::proximate | 1 | implemented, tested (passthrough + routing) |
| FS-20 | fs::last_write_time | 1 | implemented, tested (passthrough + routing) |
| FS-21 | fs::remove_all | 1 | implemented, tested (passthrough + routing) |
| FS-22 | fs::resize_file | 1 | implemented, tested (passthrough + routing) |
| FS-23 | fs::remove | 21 | implemented, tested (passthrough + routing) |
| FS-24 | fs::rename | 1 | implemented, tested (passthrough + routing) |

## C stdio (STDIO) — include/rwzfs/io/stdio.hpp
| ID | Call | Calls | Status |
|---|---|---|---|
| STDIO-01 | fread | 295 | declared |
| STDIO-02 | fopen | 68 | declared |
| STDIO-03 | ftell | 80 | declared |
| STDIO-04 | fflush | 12 | declared |
| STDIO-05 | remove (C) | 21 | declared |
| STDIO-06 | rename (C) | 1 | declared |
| STDIO-07 | ifstream | 2 | declared |
| STDIO-08 | ofstream | 2 | declared |

## POSIX — include/rwzfs/io/posix.hpp
| ID | Call | Calls | Status |
|---|---|---|---|
| POSIX-01 | close | 168 | implemented, tested |
| POSIX-02 | read | 82 | implemented, tested |
| POSIX-03 | write | 71 | implemented, tested |
| POSIX-04 | open | 34 | implemented, tested |
| POSIX-05 | stat | 30 | declared |
| POSIX-06 | ioctl | 23 | declared |
| POSIX-07 | seek (lseek) | 22 | implemented, tested |
| POSIX-08 | unlink | 2 | declared |
| POSIX-09 | mmap | 9 | declared |
| POSIX-10 | realpath | 12 | declared |
| POSIX-11 | opendir | 3 | declared |
| POSIX-12 | readdir | 4 | declared |
| POSIX-13 | closedir | 3 | declared |
| POSIX-14 | mkdir | 2 | declared |
| POSIX-15 | rmdir | 1 | declared |
| POSIX-16 | truncate | 2 | declared |
| POSIX-17 | access | 1 | declared |
| POSIX-18 | fsync | 1 | implemented, tested |
| POSIX-19 | getcwd | 2 | implemented, tested |
| POSIX-20 | glob | 1 | declared |

## Win32 — include/rwzfs/io/win32.hpp
| ID | Call | Calls | Status |
|---|---|---|---|
| WIN-01 | CreateFile | 3 | declared |
| WIN-02 | ReadFile | 2 | declared |
| WIN-03 | SetFilePointer | 11 | declared |
| WIN-04 | FindFirstFile | 2 | declared |
| WIN-05 | CreateDirectory | 1 | declared |

## Working directory (CWD) — include/rwzfs/fs/cwd.hpp, docs/cwd_design.md
| ID | Call | Calls | Status |
|---|---|---|---|
| CWD-01 | fs::current_path() getter (was FS-14) | 2 | implemented, tested |
| CWD-02 | fs::current_path(p) setter | new | implemented, tested |
| CWD-03 | io::posix::chdir | new | implemented, tested |
| CWD-04 | io::posix::getcwd (was POSIX-19) | 2 | implemented, tested |
| CWD-05 | fs::resolve | internal | implemented, tested |

## Open questions
- `close`, `seek`, `write`, `remove`, `rename`, `truncate` are ambiguous between POSIX and C stdio. I assigned `close`, `seek`, `write`, and `truncate` to POSIX, since the matching C names (`fclose`, `fseek`, `fwrite`) are absent from the list. Confirm this.
- `fs::path` is a type alias, not a wrapper. Construction is PATH-04.
- Not in the list, but needed for a portable API: `CloseHandle` and the Win32 `WIN32_FIND_DATAW` handling. Add them when their chunks are implemented.
- `ioctl` is variadic in C. The declaration takes one `void*` argument, which covers the uses I expect. Confirm against the call sites.
