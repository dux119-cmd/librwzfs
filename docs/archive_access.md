# Archive access design

## Layers
1. `rwzfs::archive::backend` (`include/rwzfs/archive/backend.hpp`): read-only, dwarfs-free interface. `lookup`, `read`, `list` take archive-relative text.
2. `rwzfs::archive::dwarfs_backend` (`src/archive/dwarfs_backend.cpp`): the only code that includes dwarfs headers. It is compiled only in the dwarfs-linked build.
3. Mount table (`include/rwzfs/fs/mount_table.hpp`): each mountpoint holds an optional backend. `find(path)` routes an absolute path to the longest matching mountpoint and returns the archive-relative text.
4. Wrappers: FS (`exists`, `status`, `is_regular_file`, `is_directory`, `current_path(p)`), POSIX (`open`, `read`, `lseek`, `close`, `fsync`, `stat`, `access`, `realpath`, `opendir`, `readdir`, `closedir`, `chdir`), dispatch to `io::archive_io`.

## Descriptors and DIR handles
- Archive descriptors are numbered from `archive_io::kFdBase` (`0x40000000`). Host descriptors are far below that, so `is_fd` is a range check.
- Each archive descriptor holds its backend, inode, size, and offset. Reads go through `backend::read` with the offset, so the reader needs no open handle.
- `DIR*` for archives is the address of an internal state object. It is registered in a table, so `readdir` and `closedir` can tell archive handles from host handles without dereferencing unknown pointers.
- Listings include `.` and `..`, then the children.

## Errors
| Situation | errno |
|---|---|
| Path has no backend (mounted without an archive) | `ENOTSUP` |
| Missing entry | `ENOENT` |
| Directory opened for reading | `EISDIR` |
| `opendir` on a file, `chdir` into a file | `ENOTDIR` |
| Write, create, truncate, unlink, mkdir, rmdir on archive | `EROFS` |
| Write to archive descriptor | `EBADF` |
| Negative seek result | `EINVAL` |
| `ioctl` / `mmap` on archive descriptor | `ENOTTY` / `ENODEV` |

## Stdio streams
- `fopen` on an archive file reads the whole file into memory and opens it with `fmemopen` in read-only mode. Memory use is the file size. Large files will need a streaming design.
- Write modes (`w`, `a`, `+`) fail with `EROFS`. Writes to a stream fail, because the stream is read-only.
- Close archive streams with `rwzfs::io::stdio::fclose` (STDIO-09). It frees the buffer. Closing one with `std::fclose` leaks the buffer.
- `fmemopen` is POSIX. It is confirmed on glibc here. Check macOS and BSD before relying on it.
- Empty archive files return a stream on `/dev/null`.
- `input_file_stream` and `output_file_stream` return failed streams for archive paths. A C++ stream cannot wrap an in-memory copy.

## Mutations and queries
- Mutations on archive paths return `EROFS` (`read_only_file_system`), including `remove`, `rename`, `permissions`, `resize_file`, and `copy` into the archive.
- `canonical` returns the normalized absolute archive path. `equivalent` compares backend and inode.
- `is_empty` is true for a directory with no children and for a zero-length file.
- `read_symlink` returns `EINVAL` for non-links and `ENOTSUP` for links, since targets are not readable yet.

## Limitations
- Symlinks inside an archive are reported as symlinks and are not followed. `open` on one returns `ENOTSUP`.
- `directory_iterator` and `recursive_directory_iterator` are not supported for archive paths. They return standard library iterator types, which cannot represent archive entries. A new iterator type is needed.
- `last_write_time` and `space` are not supported for archive paths, since entries carry no timestamps.
- Archive reads are read-only. Overlay writes are not implemented.
- `glob` does not list archive entries.
- Archive descriptor numbers are never reused, so a long-running process can exhaust the integer range only after about 2^30 opens.
