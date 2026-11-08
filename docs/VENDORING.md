# Vendoring rwzfs into an existing project

`tools/rwzfs_vendor.py` takes a CMake C++ project and switches its IO calls to rwzfs. It is written
for projects like DOSBox Staging: C++ sources, standard C and POSIX IO calls, CMake.

    python3 tools/rwzfs_vendor.py <project-root> --dry-run --diff     # look first
    python3 tools/rwzfs_vendor.py <project-root> --report vendor.txt  # then apply

Python 3.9 or newer, standard library only. Run it from a clean git checkout, and review the diff
afterwards (`git diff`).

## What it does

1. **Copies the library** (`include/`, `src/`, `CMakeLists.txt`, `LICENSE`) to `<root>/src/libs/rwzfs`
   (change with `--libdir`). An existing copy there is replaced.
2. **Edits the top-level `CMakeLists.txt`**: appends a block that sets `RWZFS_WITH_DWARFS` (off unless
   `--with-dwarfs`), turns off the library's tests and examples, adds the library subdirectory, and
   adds `target_link_libraries(<target> PRIVATE rwzfs)` for each `add_executable`/`add_library` in that
   file. The block is marked, and it is not added twice.
3. **Rewrites sources.** Every `.cpp`, `.cc`, `.cxx`, `.hpp`, `.h` (C++ headers) and similar file under
   the project, except `build*`, `.git`, `external/`, `third_party/` and the vendored library:
   - calls to standard and POSIX IO functions become library wrappers: `open` → `rwzfs::io::posix::open`,
     `fopen` → `rwzfs::io::stdio::fopen`, `CreateFileW` → `rwzfs::io::win32::create_file`, and so on
     (the full list is in the script);
   - `#include "rwzfs/io/<family>.hpp"` is added after the last include, for each family the file uses;
   - C headers are dropped when none of their symbols remain (`<fcntl.h>`, `<unistd.h>`,
     `<sys/stat.h>`, `<dirent.h>`, `<cstdio>`, and others). A header is kept if anything it provides is
     still used.
4. **Writes a report**: rewritten call counts, headers added and dropped, every skipped call site with
   file and line, and everything that needs a person.

## What it leaves alone, and why

A call is skipped (and listed) when:
- it is a member call (`x.read(...)`, `p->open(...)`);
- it looks like a declaration or definition (`int read(int);`, `struct R { int read(); }`);
- it is inside a string literal or a comment;
- its name is common (`open`, `read`, `write`, `close`, `remove`, `rename`, ...) and the file does not
  include the C header that declares it. Without that header the call is probably someone else's;
- it is `std::remove` or `std::rename` with an argument that does not look like a path. Those are
  algorithms and erase-remove idioms.

Nothing is rewritten in C sources (`.c`). The library is C++ only; those files are listed in the report.

Anything the script does not rewrite is still a correct program: the original call keeps calling the
host C library, and it does not go through the archive.

## What it cannot do, and what a person must check

- **std::filesystem.** The namespace `std::filesystem` is recognised under its full name, under the
  project shorthand `std_fs`, and under any alias declared in the file as
  `namespace X = std::filesystem;` (or `= std_fs;`). A call that already passes an error code
  (`std_fs::exists(p, ec)`) is rewritten to `rwzfs::fs::exists(p, ec)`: the name changes and nothing
  else does. A call without an error code throws in the standard form, so the script leaves it and lists
  it in the report: port it by hand, adding an `std::error_code` and checking it. Type names such as
  `std_fs::path` are never changed (`rwzfs::fs::path` is the same type).
- **C++ streams.** `std::ifstream`/`std::ofstream` are not rewritten. The library has
  `rwzfs::io::stdio::input_file_stream` and `output_file_stream`, but an archive path gives a failed
  stream, and a stream cannot wrap an in-memory archive copy. Listed in the report.
- **C++23.** The library needs C++23 (`std::expected`). The script warns when `CMAKE_CXX_STANDARD` is
  below 23. The library sets 23 for its own targets; the project's targets that include library headers
  must also use 23.
- **Targets in subdirectories.** Only targets declared in the top-level `CMakeLists.txt` are linked
  automatically. Add `target_link_libraries(<t> PRIVATE rwzfs)` for the others.
- **Regex, not a parser.** The rewrite works line by line. Multi-line comments and string literals that
  contain code are the places where it can be wrong. Review the diff.
- **Windows.** `win32::` mappings compile only on Windows (the Win32 wrappers' CI job compiles them). The
  Windows output of a vendored project has not been tested.

## Library behaviour changes for drop-in use

- The stdio and POSIX wrappers are not `[[nodiscard]]`, because they mirror C functions whose results are
  often ignored (`fread`, `write`).

## Tested

The sample project also uses `std_fs::` (the project shorthand) and an `fs` alias. Calls with an error code
are rewritten to `rwzfs::fs::`; `std_fs::remove(name)` without one is listed in the report.

`tools/rwzfs_vendor.py` was run on a small sample project (C++ with the cases above, plus a C file).
The sample was built with CMake against the vendored library (without dwarfs) and runs with the expected
output. The rewritten sources build with no warnings. The report listed the skipped cases and the
C file, and the include changes were as expected.

Sample rewrite, `io.cpp`:

    -  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    +  int fd = rwzfs::io::posix::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    -  std::FILE* f = fopen(path, "rb");
    +  std::FILE* f = rwzfs::io::stdio::fopen(path, "rb");
