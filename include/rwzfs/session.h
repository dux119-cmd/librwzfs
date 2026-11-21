#pragma once

// The high-level interface: mount an archive for a scope, read its changes,
// revise it, and create archives from directories. Each call is one line for
// the common cases; the lower-level functions in mount.h, revise.h and
// archive/create.h remain for code that needs them.

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>

#include "rwzfs/archive/create.h"
#include "rwzfs/mount.h"
#include "rwzfs/revise.h"

namespace rwz {

struct session_options {
	// Where the overlay (the changes) is kept. Empty: the library's default
	// location.
	std::optional<std::filesystem::path> overlay_root = {};
};

// What the overlay holds, as counted from its layout (data/, whiteouts/, opaque/).
struct overlay_changes {
	std::size_t written  = 0; // files added or changed
	std::size_t deleted  = 0; // archive files hidden by whiteouts
	std::size_t new_dirs = 0; // directories recreated over deleted ones

	[[nodiscard]] bool empty() const noexcept
	{
		return written + deleted + new_dirs == 0;
	}
};

// An archive mounted at the archive root for as long as the object lives. The
// destructor unmounts it.
//
//   auto game = rwz::session::open("Game (USA).dwarfs");
//   if (!game) { ... }
//   std::FILE* rom = rwz::stdio::fopen((game->root() /
//   "Game.nes").string().c_str(), "rb");
class session {
public:
	[[nodiscard]] static std::expected<session, MountError> open(
	        const std::filesystem::path& archive,
	        const session_options& options = {});

	session(session&& other) noexcept;
	session& operator=(session&& other) noexcept;
	session(const session&)            = delete;
	session& operator=(const session&) = delete;
	~session();

	// The merged view's root: the archive root ("/rwz" on POSIX, "C:/RWZ"
	// on Windows).
	[[nodiscard]] const std::filesystem::path& root() const noexcept
	{
		return root_;
	}

	// The overlay's changes so far.
	[[nodiscard]] overlay_changes changes() const;

	// Writes a revised archive (<stem>-revN.dwarfs beside the original)
	// from the merged view. The original is not changed, and the session
	// stays mounted; the overlay is cleared when the revision is written.
	[[nodiscard]] std::expected<std::filesystem::path, revise_error> revise(
	        const create_options& options = {});

	// Unmounts now rather than at the end of the scope. Safe to call twice.
	void close() noexcept;

private:
	session(std::filesystem::path root, std::filesystem::path archive);

	std::filesystem::path root_    = {};
	std::filesystem::path archive_ = {};
	bool mounted_                  = false;
};

// Creates <name>.dwarfs from a game directory. `name` may be a bare name ("Game
// (USA)"), a file name with the extension, or a path. A bare name is written to
// the current directory. An existing file is never overwritten
// (create_error::already_exists).
[[nodiscard]] std::expected<std::filesystem::path, create_error> archive_directory(
        const std::filesystem::path& name, const std::filesystem::path& source,
        const create_options& options = {});

} // namespace rwz
