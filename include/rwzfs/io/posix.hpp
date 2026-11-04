#pragma once

#if !defined(_WIN32)

#include <dirent.h>
#include <fcntl.h>
#include <glob.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <cstddef>

namespace rwzfs::io::posix {

// POSIX-01 (168x) `close` on a file descriptor. `fclose` is not in the call list.
int close(int fd);
// POSIX-02 (82x)
ssize_t read(int fd, void* buf, std::size_t count);
// POSIX-03 (71x)
ssize_t write(int fd, const void* buf, std::size_t count);
// POSIX-04 (34x)
int open(const char* path, int flags, mode_t mode);
// POSIX-05 (30x)
int stat(const char* path, struct stat* buf);
// POSIX-06 (23x)
int ioctl(int fd, unsigned long request, void* arg);
// POSIX-07 (22x) `seek`, mapped to lseek. Confirm against the call site.
off_t lseek(int fd, off_t offset, int whence);
// POSIX-08 (2x)
int unlink(const char* path);
// POSIX-09 (9x)
void* mmap(void* addr, std::size_t length, int prot, int flags, int fd, off_t offset);
// POSIX-10 (12x)
char* realpath(const char* path, char* resolved);
// POSIX-11 (3x)
DIR* opendir(const char* path);
// POSIX-12 (4x)
struct dirent* readdir(DIR* dir);
// POSIX-13 (3x)
int closedir(DIR* dir);
// POSIX-14 (2x)
int mkdir(const char* path, mode_t mode);
// POSIX-15 (1x)
int rmdir(const char* path);
// POSIX-16 (2x)
int truncate(const char* path, off_t length);
// POSIX-17 (1x)
int access(const char* path, int mode);
// POSIX-18 (1x)
int fsync(int fd);
// POSIX-19 (2x) / CWD-04: returns the archive cwd when one is set.
char* getcwd(char* buf, std::size_t size);
// CWD-03 (new, not in the call list)
int chdir(const char* path);

// METADATA (new). Archive files with an overlay are copied up before the change. Directories
// cannot change permissions or times through these calls (EPERM).
int chmod(const char* path, mode_t mode);
int utimensat(int dirfd, const char* path, const struct timespec times[2], int flags);
// POSIX-20 (1x)
int glob(const char* pattern, int flags, int (*errfunc)(const char*, int), glob_t* out);

}  // namespace rwzfs::io::posix

#endif  // !defined(_WIN32)
