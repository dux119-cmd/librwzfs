# rwzfs

A C++23 library that lets a program read files from a compressed, read-only archive, and write to them
through a private overlay, using the same calls it already makes for ordinary files. It is built for
emulators and preservation tools, where a game collection is large, read far more often than it changes,
and must not be silently modified.

rwzfs works by putting a *merged view* at a fixed root (`/rwz` on POSIX, `C:/RWZ` on Windows). Paths under
that root come from the archive, with any changes made in an overlay directory on top. Paths outside the
root go to the host filesystem unchanged. A program that never mounts an archive behaves exactly as it
did before.

## Why it helps an emulation project

### Many releases of the same game, packed once

A preservation collection holds the same game many times over: regions (NTSC-U, PAL, NTSC-J), revisions
(1.0, 1.1), hacks and translations, betas and prototypes, and special editions. Most of the bytes in these
releases are identical. Stored as loose folders, each copy costs its full size. Stored in one archive, the
archive is built from the whole set at once.

The archive is a [DwarFS](https://github.com/mhx/dwarfs) image. DwarFS splits file content into blocks and
deduplicates them across the whole image, so a block shared by a hundred releases is stored once. It then
compresses what remains (zstd by default). For a set of near-identical releases, the
result is typically a small fraction of the size of the loose folders. The exact figure depends on the
content, so measure your own collection rather than taking a number from here.

The same archive keeps every version addressable under its own path, so the emulator can list, open and
read any release without unpacking anything.

### Reads are fast, and the archive stays read-only

- **Random access.** Reads fetch only the blocks they need, through DwarFS's block cache, with read-ahead
  (64 KiB by default). A game does not have to be unpacked to be played.
- **Nothing in the archive changes.** Every write goes to the overlay. A bug in a save routine cannot
  corrupt a preserved image. The archive file's checksum stays the same until you deliberately revise it.
- **One process at a time.** A lock file prevents two emulator instances from writing the same overlay.

### Saves, patches and changes stay separate

Writes land in a per-archive overlay directory. Any file the program writes shadows the archive copy, and
deleting a file in the view writes a whiteout, so the archive is never touched. You can keep save data in
the overlay, then decide what to keep:

- **Revise** writes the merged view (archive plus overlay) to a new archive, `game-rev1.dwarfs`, and clears
  the overlay. Use it when a patch or a save state should become part of the collection.
- **Leave it** and the overlay keeps the changes, while the original archive stays as preserved.

Revision reads each file once from the merged view. Keep saves you do not want in the preserved archive
outside the overlay's archive, or copy them out before you revise.

### One API for host files and archive files

The library gives you wrappers with the names you already know:

- POSIX: `rwz::posix::open`, `read`, `write`, `lseek`, `stat`, `opendir`, `readdir`, `chmod`, `unlink`, and
  the rest of the POSIX wrappers.
- C stdio: `rwz::stdio::fopen`, `fread`, `fwrite`, `fseek`, `ftell`, `fclose`.
- C++: `rwz::stdio::file_istream` and `file_ostream`, which are `std::istream` and `std::ostream` with the
  standard constructors, `open()`, `close()` and openmode flags, over the same wrappers.
- Windows: `rwz::win32::CreateFileW`, `ReadFile`, the find functions, `DeleteFileW`, `RemoveDirectoryW`.
- `rwz::fs`: `std::filesystem`-style functions. Each has a form that takes an `std::error_code` and a form
  that throws the same `std::filesystem::filesystem_error` the standard library throws.

Code that already uses these calls can switch with a search and replace, or with the included vendoring
tool (below), without changing how it handles errors.

### Works in the project you already have

- **Vendoring tool.** `tools/rwzfs_vendor.py` takes a CMake C++ project and rewrites its calls to the
  rwzfs wrappers, one replacement per git commit. Bisecting then finds the exact change that introduced a
  problem. It flags calls that might refer to the project's own functions, which matters in emulators
  (I/O-port `read` and `write` in a hardware model, for example), and it keeps those for a person to
  review. `--refresh` updates the vendored copy to the current library in one commit.
- **Managed dependencies.** The build fetches and builds dwarfs and its dependencies from pinned versions
  (`scripts/deps/versions.sh`), with SHA-256 checks on the release archives. They go into `build-deps/`,
  beside `build/`, so every build (release, debug, sanitizer) shares them, and cleaning `build/` keeps them.
  Set `RWZFS_USE_MANAGED_DEPS=OFF` to use system libraries instead.
- **Sanitizer builds.** `cmake --preset sanitize` builds with AddressSanitizer and UndefinedBehaviorSanitizer
  and runs the test suite under them.

## What it does on each platform

| | Linux | macOS | Windows |
|---|---|---|---|
| Archive read, random access | yes | yes | yes |
| Overlay writes, copy-up | yes | yes | yes |
| C stdio on archive files | callback stream, no copy | callback stream, no copy | read-only temporary copy, removed on `fclose` |
| Create and revise archives | yes | yes | yes |
| Directory listing, stat, rename, delete | yes | yes | yes |
| Ownership | not stored (zip-like: the mounting user owns the view) | same | same |

Windows has no callback-based `FILE*`, so C stdio reads of an archive file copy that file to a temporary
file (deleted when the stream closes). `CreateFileW` and `ReadFile` on archive files read in chunks, with
no copy.

## Limits

- **Read-only archives.** Changes go to the overlay. Editing an archive means revising it.
- **Modification times** are stored to the second. **Access times** are not stored.
- **Ownership** is not stored. Like a zip file, whoever mounts the archive owns everything in it.
- **Symlinks** in the archive are followed. You cannot create or rename symlinks in the overlay.
- **Host symlinks** that point into the archive root are not followed into the archive.
- **Standard C++ streams** that are not replaced by `rwz::stdio` calls (for example `std::ifstream` built
  directly from a path) bypass the wrappers. The vendoring tool replaces the common forms.
- **One process per archive** at a time.
- **Creating an archive** needs the dwarfs writer, which comes from the managed dependencies or a system
  install.

## Command-line tool

`rwz-tool` opens an archive with its overlay (`open`), writes a revised archive from the changes in an
overlay (`revise`), and creates an archive from a game directory (`create`). See `docs/USAGE.md`.

## Building

Requirements: CMake 3.21 or newer, a C++23 compiler (GCC 14 is what the test suite runs on; Clang and MSVC
are built by CI), bash, git, curl, and, for the managed dependencies, a long build (hours on one core).

    cmake -S . -B build                       # managed dependencies, built once into build-deps/
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure

Without dwarfs (no archive support, the POSIX and Windows wrappers still work):

    cmake -S . -B build -DRWZFS_WITH_DWARFS=OFF

See `docs/USAGE.md` for a walk-through, `docs/archive_creation.md` for archive creation, and
`docs/HANDOVER.md` for the status of each part.

## Using it

    #include "rwzfs/rwz.h"

    rwz::mount_options options = {};                       // where saves and changes go
    options.write_root = "/home/me/.local/state/rwz";      // relative paths resolve at mount time
    rwz::mount_archive("/collections/Game (USA) (Rev 1).dwarfs", "/rwz", options);

    std::FILE* rom = rwz::stdio::fopen("/rwz/Game.nes", "rb");
    // ... the emulator reads the image exactly as it would from a file ...
    rwz::stdio::fclose(rom);

    rwz::unmount_archive("/rwz");

A complete program is in `examples/usage.cpp`. Run it to see every operation on a host tree, a mounted
archive, and a revised archive, side by side.

## Documentation

- `docs/USAGE.md`: the API, with examples
- `docs/archive_creation.md`: how archives are created and revised
- `docs/archive_access.md`, `docs/overlay_design.md`, `docs/cwd_design.md`: how paths and writes behave
- `docs/VENDORING.md`: the vendoring tool
- `docs/STATIC_BUILD.md`: the managed dependencies
- `docs/WINDOWS_STDIO_PLAN.md`: how C stdio works on Windows
- `docs/HANDOVER.md`: status, open items, and the coding standard

## License

No license file has been added to this tree yet. Add one before distributing the library.
