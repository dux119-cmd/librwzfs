#pragma once

// Umbrella header: the wrapper namespaces rwz::posix, rwz::stdio, rwz::win32
// and rwz::fs, and the archive, overlay and mount API in rwzfs. Include this to
// get everything.

#include "rwzfs/archive/create.h"
#include "rwzfs/fs/archive_path.h"
#include "rwzfs/fs/filesystem.h"
#include "rwzfs/io/stdio.h"
#include "rwzfs/mount.h"
#include "rwzfs/revise.h"

#if defined(_WIN32)
#include "rwzfs/io/win32.h"
#else
#include "rwzfs/io/posix.h"
#endif
