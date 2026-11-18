# Windows: C stdio and C++ file IO on archives

Goal: C stdio (`fopen`, `fread`, `fseek`, `ftell`, `fclose`) and C++ file IO work on archive and
overlay paths on Windows, as they do on POSIX.

## Constraint

The MSVC CRT cannot give a `FILE*` that lives only in memory. There is no `fmemopen` and no
callback-stream hook (`fopencookie` and `funopen` do not exist). A `FILE*` always sits on a file
descriptor, so every workaround puts the bytes in a real file.

## Options considered

| option | memory | disk | status |
|---|---|---|---|
| Stream chunks from the archive (callback stream) | chunk-sized | none | not possible on Windows |
| Full read into a temporary file, deleted on `fclose` | page cache first | temporary file | **chosen** (Windows only) |
| Full read into a memory-backed section | yes | none | `FILE*` cannot read it; no CRT path |
| Ask the user to use `CreateFileW` | chunk-sized | none | already supported; not a stdio replacement |

## Chosen design (Windows only)

- **Read-only archive files.** `fopen` reads the whole file in chunks from the backend, writes it to a
  temporary file in `std::filesystem::temp_directory_path()`, rewinds it, and returns its `FILE*`
  opened `w+b` and then read from. The bytes are read once, at open. Nothing else changes.
- **Temporary file lifetime.** The path is recorded with the `FILE*`. `fclose` closes the stream and
  removes the file. A crash can leave a file behind; it is named `rwz-<random>.tmp`, so it is easy to
  clean up.
- **Writes.** Unchanged. Archive files copy up into the overlay on first write (real files), and
  overlay files are real files already.
- **Disk use.** Windows keeps recently written temporary files in the page cache, so most copies
  stay in memory until memory pressure. The copy is the size of the archive file.

## What this makes work

- C stdio on archive and overlay paths, through the `rwz::stdio` wrappers and the vendored
  call sites.
- `std::filesystem` calls that take an `error_code`, through `rwz::fs`.

## What stays out of scope

- `std::ifstream` / `std::ofstream` that are not rewritten (for example, members `open()` and `close()`).
  The vendoring tool rewrites the declarations and reports the rest.
- Files larger than the temporary space available. The copy needs that space.

## Plan and status

1. Windows `fopen` of a read-only archive file: full read into a temporary file, `fclose` removes it.
   Implemented in `src/io/stdio.cpp` (`open_archive_copy`). Test: `tests/io/stdio_windows_test.cpp` (content, seek, read-only writes, missing file, cleanup). Tested on Windows CI once it runs.
2. C++ streams. `input_file_stream` and `output_file_stream` return `rwz::stdio::file_istream` and `file_ostream`: `std::istream` and `std::ostream` subclasses over the stdio wrappers. They read and write archive and overlay paths on every platform (on Windows, through the same temporary copy). Tests: `tests/io/stdio_streams_test.cpp`. Done.
3. Vendored call sites. `tools/rwzfs_vendor.py` rewrites `std::ifstream` / `std::ofstream` to the new types and adds the stdio include. Verified on Linux with a sample project (builds, reads, writes). The Windows sample is not yet run.
