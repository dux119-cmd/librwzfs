# Vendoring rwzfs into an existing project

`tools/rwzfs_vendor.py` takes a CMake C++ project and switches its IO calls to rwzfs, interactively,
one change per git commit. Bisecting then finds the exact replacement that introduced a problem.

    git switch -c vendor-rwzfs                     # a branch of your own
    python3 tools/rwzfs_vendor.py <project-root> --branch vendor-rwzfs --dry-run   # list the sites
    python3 tools/rwzfs_vendor.py <project-root>                                   # the real run

Python 3.9 or newer, standard library only. The project must be a git work tree. The tool refuses a
dirty tree (commit or stash first, or pass `--allow-dirty`).

## The flow

1. **Library** (one commit): copies the library to `<libdir>` (default `src/libs/rwzfs`) and links it
   to the top-level targets. Asked once.
2. **Headers** (one commit): the rwzfs headers the replacements need, for every file, before any code
   changes.
3. **Replacements** (one commit each): for each call or type the tool can replace, it shows the file,
   line, original text, replacement and reason, and asks `[y]es / [n]o (skip) / [q]uit`. A yes applies
   that one change and commits it.
4. **Sites that need a person** (one commit each, when you save): the tool shows a suggested signature
   and waits. Edit the file in your editor, save, and press Enter; the file's changes are committed.
   `s` skips the site, `q` quits.
5. **Unused C headers** (one commit each): `[y]es / [n]o / [q]uit`.

Every commit message holds the generated record: file and line, original text, replacement, reason,
and the line before and after. Commit subjects name the change, so `git log --oneline` reads as the
list of replacements, and `git bisect` lands on the first bad one.

You can stop at any time (`q`) and rerun later: committed changes stay, and the tool rescans for what
remains. Skipped sites are not offered again in one run.

## Options

| option | meaning |
|---|---|
| `--branch NAME` | create and switch to a new branch first |
| `--dry-run` | list the sites and change nothing |
| `--libdir DIR` | where the library goes (default `src/libs/rwzfs`) |
| `--no-dwarfs` | vendor without the dwarfs archive reader (no managed dependencies are built) |
| `--library PATH` | the rwzfs source tree to copy (default: the tree this script is in) |
| `--exclude DIR ...` | extra directories to skip |
| `--allow-dirty` | run with uncommitted changes |

## Names

Replacements use the flat namespaces of `rwzfs/rwz.h`:

- `rwz::posix::` (POSIX), `rwz::win32::` (Windows), `rwz::stdio::` and `rwz::fs::`

Each file that needs one gets a single `#include "rwzfs/rwz.h"`.

## Local definitions

Before replacing a call, the tool scans the file it is working on for functions with the same name. A
file that defines or declares its own `read`, `write`, `open` and so on (for example, I/O-port emulation)
gets its calls to that name listed as sites that need a person, with the line of the local definition. A
definition in another file does not affect the call: the call still refers to the C function unless its
own file declares the name. A call the tool cannot tie to a definition (a return type on an earlier line,
for instance) is still replaced, so check the dry run for names that look local.

## Rules it applies

1. **Copies the library** (`include/`, `src/`, `CMakeLists.txt`, `LICENSE`) to `<root>/src/libs/rwzfs`
   (change with `--libdir`). An existing copy there is replaced.
2. **Edits the top-level `CMakeLists.txt`**: appends a block that sets `RWZFS_WITH_DWARFS` (off unless
   adds the library subdirectory (its tests and examples are off unless the library is built in Debug), and
   adds `target_link_libraries(<target> PRIVATE rwzfs)` for each `add_executable`/`add_library` in that
   file. The block is marked, and it is not added twice.
3. **Rewrites sources.** Every `.cpp`, `.cc`, `.cxx`, `.hpp`, `.h` (C++ headers) and similar file under
   the project, except `build*`, `.git`, `external/`, `third_party/` and the vendored library:
   - calls to standard and POSIX IO functions become library wrappers: `open` → `rwz::posix::open`,
     `fopen` → `rwz::stdio::fopen`, `CreateFileW` → `rwz::win32::create_file`, and so on
     (the full list is in the script);
   - `#include "rwzfs/io/<family>.hpp"` is added after the last include, for each family the file uses;
   - C headers are dropped from `.cpp` files when none of their symbols remain (`<fcntl.h>`,
     `<unistd.h>`, `<sys/stat.h>`, `<dirent.h>`, `<cstdio>`, and others). Headers (`.hpp`, `.h`) never
     lose an include, since files that include them may rely on it.
4. **Writes a report**: rewritten call counts, headers added and dropped, every skipped call site with
   file and line, and everything that needs a person.

## Sites that need a person

Each site the tool cannot replace with confidence is shown with a suggested signature, for example
`rwz::fs::exists(p, ec)` for a `std::filesystem` call without an error code. Edit the file, save, and
press Enter. The tool commits the file's changes as one commit with the generated message. Those sites
are typically:
- `std::filesystem` calls without an `std::error_code` for functions that have no throwing form (see USAGE);
- an unqualified C name (`read`, `write`, `open`) whose header is not included in the file, which may be
  another function with the same name;
- `remove` or `rename` whose first argument does not look like a path;

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
  (`std_fs::exists(p, ec)`) is rewritten to `rwz::fs::exists(p, ec)`: the name changes and nothing
  else does. A call without an error code throws in the standard form, so the script leaves it and lists
  it in the report: port it by hand, adding an `std::error_code` and checking it. Type names such as
  `std_fs::path` are never changed (`rwz::fs::path` is the same type).
- **C++ streams.** `std::ifstream` and `std::ofstream` declarations are rewritten to
  `rwz::stdio::file_istream` and `file_ostream`. The rwz streams have the same `open()`, `close()`, constructors and openmode flags, so those calls need no change.
- **C++23.** The library needs C++23 (`std::expected`). The script warns when `CMAKE_CXX_STANDARD` is
  below 23. The library sets 23 for its own targets; the project's targets that include library headers
  must also use 23.
- **Targets in subdirectories.** Only targets declared in the top-level `CMakeLists.txt` are linked
  automatically. Add `target_link_libraries(<t> PRIVATE rwzfs)` for the others.
- **Regex, not a parser.** The rewrite works line by line. Multi-line comments and string literals that
  contain code are the places where it can be wrong. Review the diff.
- **Windows.** `win32::` mappings compile only on Windows, and the Win32 layer is covered by the Windows CI job. The vendoring script itself has not been run on a Windows project.

## Library behaviour changes for drop-in use

- The stdio and POSIX wrappers are not `[[nodiscard]]`, because they mirror C functions whose results are
  often ignored (`fread`, `write`).

## Tested

The tool was run on a small git project with an `open`, `read`, `fopen`, `fclose`, `std::remove`,
`close` and an unused header. Each replacement was one commit with its generated message; the header
commit came before the code commits; the manual site, edited by hand, was committed as one commit. The
result built with CMake. The rwzfs side of the replacements is covered by the library's own tests.

## Refreshing the vendored library

When rwzfs changes, bring a project's vendored copy up to date with one command:

    python3 tools/rwzfs_vendor.py <project-root> --refresh [--library PATH]

It replaces `<libdir>` with the current library and commits the change alone, with the library's content id
and source revision in the message. It asks before committing. It does not touch your code, the CMake
lines, or any replacement: the code keeps calling `rwz::` names, which the new library still provides.

- The copy records its content id in `<libdir>/.vendored-from`. The up-to-date test compares the files
  themselves, so a stamp cannot claim a state the files do not have.
- It refuses to run over uncommitted changes in the vendored directory (`--allow-dirty` overrides this,
  and the commit then includes them, so use it deliberately).
- The generated `rwzfs` block in your top-level `CMakeLists.txt` is refreshed too, as its own commit. It keeps
  your `target_link_libraries(... rwzfs)` lines (an `add_library(<name> INTERFACE)` target links
  `INTERFACE`, which CMake requires) and rewrites the rest to match the library (dwarfs on by
  default; pass `--no-dwarfs` to keep it off). The dwarfs setting uses `FORCE`, so an old cached `OFF` cannot
  override the block. If you edited the block by hand, the refresh shows the change and asks first.
- Declining the prompt, or any error after the copy, restores the directory to the last commit.
- Projects vendored before this option have no stamp; the first refresh treats them as unknown and
  replaces the copy if it differs.

Check the library's API changes against your code before you refresh: a removed or renamed function
breaks the build, which the refresh does not check.

## The dependencies come with the library

The vendored copy includes `scripts/deps` (the versions and the builders for dwarfs and its stack). The
generated CMake turns dwarfs on, so configuring the project builds the managed dependencies once into
`build-deps/` beside `build/`, and every later configure reuses them. The first configure needs bash (Git's
bash on Windows), git, curl and a C++23 compiler, and takes a long time: the whole stack is built from
pinned sources. Pass `--no-dwarfs` to vendor the library without the archive reader and without any of
that.

A project that already has its own dwarfs can set `-DRWZFS_DEPS=system` and `-DDWARFS_PREFIX=<prefix>` when it
configures. `--deps vcpkg|scripts|system` on the vendoring tool chooses the default the generated block sets.

## The vcpkg overlay

vcpkg reads its overlay (the dwarfs port, `vcpkg-ports/`, and the static triplets, `vcpkg-triplets/`) during
`project()`, so the vendoring tool puts the settings in a block just before the first `project()` call. Keep that
block where it is. The block appends to `VCPKG_OVERLAY_PORTS` and `VCPKG_OVERLAY_TRIPLETS` rather than replacing them,
so a project's own overlays still work: set them before the block (or before `project()`), and both sets are used. The tool copies both directories into the vendored library, and updates the block on refresh.

Your configure preset still needs the triplet, for example `VCPKG_TARGET_TRIPLET=x64-linux-static` (or
`arm64-osx-static`, `x64-windows-static-rel`), and the vcpkg toolchain. A project whose manifest has a
`builtin-baseline` also needs the port to be found through the overlay: the error "the baseline does not contain an
entry for port dwarfs" means the overlay was not seen.

## SDL file opening

SDL opens files with its own C-library I/O, which cannot see a path under the archive root. The tool therefore
rewrites SDL's file-open calls to the bridge in `integrations/sdl/`:

- `SDL_IOFromFile(...)` (SDL3) becomes `rwz_SDL_IOFromFile(...)`
- `SDL_RWFromFile(...)` (SDL2) becomes `rwz_SDL_RWFromFile(...)`

This includes the calls inside SDL_sound's own C sources in your project. The scan covers all vendored code under
`src/libs/`. The rwzfs library itself (`<libdir>`) is always skipped, whatever `--exclude` says. Third-party code there is scanned too, so its file IO is
rewritten; use `--exclude <dir>` for any vendored directory that must stay as it is. C files get only this rule (the C++ wrapper
names do not compile in C), and they get the C bridge header, not the C++ headers. Declarations in SDL's own headers
are not changed. The generated CMake block compiles `rwz_sdl_io.cpp` into each target, so the bridge is built with the
project. The bridge is written against the public SDL3 and SDL2 APIs; compile it against your SDL version before
relying on it.

## Vendored CMake projects

A vendored project under `src/libs/` with its own `CMakeLists.txt` (riffcpp, for example) gets the rewritten calls like
any other code, so it needs the rwzfs headers and the rwzfs library. The tool does both:

- every file that uses `rwz::` includes `rwzfs/rwz.h` (one commit, made after the rewrites);
- the generated block links each target defined in those projects to rwzfs, guarded with `if(TARGET ...)`. Linking
  rwzfs also gives the target rwzfs's include directory. An `add_library(<name> INTERFACE)` target gets an `INTERFACE`
  link, which is the only form CMake accepts for it.

Run the tool (or `--refresh`) after adding a vendored project, so its target is picked up.

## MSVC and 64-bit file calls

These spellings are rewritten to the library's wrappers: `fopen_s` (kept, same return convention), `fopen64` to
`fopen`, `_fseeki64` to `fseeko`, `_ftelli64` to `ftello`, and the wide-character `_wfopen` and `_wfopen_s` (on
Windows; the path is narrowed to the ANSI form the wrappers take, so names outside the system code page cannot be
represented).

A header that C++ sources include is compiled as C++, so the tool rewrites it even when its text looks like C (for
example a single-header library such as dr_mp3.h). The tool follows includes transitively from the C++ sources.
