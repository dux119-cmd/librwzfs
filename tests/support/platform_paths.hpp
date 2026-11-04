#pragma once

// Absolute path literals for tests. On POSIX: "/rwz" and "/". On Windows the archive root is
// "C:/RWZ" and host absolute paths need a drive, so "C:/". Use as: RWZ_ROOT "/DIR/FILE.DAT".
#if defined(_WIN32)
#define RWZ_ROOT "C:/RWZ"
#define HOST_ROOT "C:/"
#else
#define RWZ_ROOT "/rwz"
#define HOST_ROOT "/"
#endif
