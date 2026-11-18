#pragma once

// Umbrella header: the wrapper namespaces rwz::posix, rwz::stdio, rwz::win32
// and rwz::fs, and the archive, overlay and mount API in rwzfs. Include this to
// get everything.

#include "rwzfs/fs/filesystem.hpp"
#include "rwzfs/io/stdio.hpp"

#if defined(_WIN32)
#include "rwzfs/io/win32.hpp"
#else
#include "rwzfs/io/posix.hpp"
#endif
