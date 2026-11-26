#include "rwzfs/session.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#include "rwzfs/fs/archive_path.h"
#include "rwzfs/fs/directory.h"
#include "rwzfs/fs/filesystem.h"
#include "rwzfs/fs/mount_table.h"
#include "rwzfs/io/stdio.h"
#include "rwzfs/overlay/lock.h"

namespace rwz {

namespace {

// The overlay layout is documented in docs/overlay_design.md: data/ holds
// copied-up and new files, whiteouts/ marks deleted archive entries, opaque/
// marks directories recreated over deleted ones.
overlay_changes count(const std::filesystem::path& overlay)
{
	overlay_changes c  = {};
	std::error_code ec = {};
	for (const auto& e :
	     std::filesystem::recursive_directory_iterator(overlay / "data", ec)) {
		if (e.is_regular_file(ec)) {
			++c.written;
		}
	}
	ec.clear();
	for (const auto& e :
	     std::filesystem::recursive_directory_iterator(overlay / "whiteouts", ec)) {
		if (e.is_regular_file(ec)) {
			++c.deleted;
		}
	}
	ec.clear();
	for (const auto& e :
	     std::filesystem::recursive_directory_iterator(overlay / "opaque", ec)) {
		if (e.is_directory(ec)) {
			++c.new_dirs;
		}
	}
	return c;
}

} // namespace

namespace {

// True when the overlay has no saved writes: its data, whiteouts and opaque
// areas are absent or empty.
bool overlay_has_no_writes(const std::filesystem::path& overlay_dir)
{
	for (const char* area : {"data", "whiteouts", "opaque"}) {
		std::error_code ec = {};
		const auto dir     = overlay_dir / area;
		if (!std::filesystem::exists(dir, ec)) {
			continue;
		}
		if (!std::filesystem::is_empty(dir, ec) || ec) {
			return false;
		}
	}
	return true;
}

// Removes an archive's whole overlay directory when it holds no writes, under
// its lock. A locked overlay (another instance) is left alone.
void remove_overlay_if_unused(const std::filesystem::path& archive,
                              const session_options& options)
{
	const auto overlay_dir = overlay::overlay_directory(
	        archive, options.overlay_root, overlay::process_environment());
	if (!overlay_dir) {
		return;
	}
	std::error_code ec = {};
	if (!std::filesystem::exists(*overlay_dir, ec)) {
		return;
	}
	if (!overlay::lock::acquire(*overlay_dir)) {
		return;
	}
	if (overlay_has_no_writes(*overlay_dir)) {
		std::filesystem::remove_all(*overlay_dir, ec);
	}
	overlay::lock::release(*overlay_dir);
}

} // namespace

std::expected<session, MountError> session::open(const std::filesystem::path& archive,
                                                 const session_options& options)
{
	mount_options mount = {};
	if (options.overlay_root) {
		mount.write_root = *options.overlay_root;
	}
	const auto root      = fs::archive_root();
	const MountError err = mount_archive(archive, root, mount);
	if (err != MountError::None) {
		return std::unexpected(err);
	}
	session s(root, archive);
	s.overlay_root_ = options.overlay_root;
	s.mounted_      = true;
	return s;
}

session::session(std::filesystem::path root, std::filesystem::path archive)
        : root_(std::move(root)),
          archive_(std::move(archive))
{}

session::session(session&& other) noexcept
        : root_(std::move(other.root_)),
          archive_(std::move(other.archive_)),
          overlay_root_(std::move(other.overlay_root_)),
          mounted_(other.mounted_)
{
	other.mounted_ = false;
}

session& session::operator=(session&& other) noexcept
{
	if (this != &other) {
		close();
		root_          = std::move(other.root_);
		archive_       = std::move(other.archive_);
		overlay_root_  = std::move(other.overlay_root_);
		mounted_       = other.mounted_;
		other.mounted_ = false;
	}
	return *this;
}

session::~session()
{
	close();
}

void session::close() noexcept
{
	if (!mounted_) {
		return;
	}
	unmount_archive(root_);
	mounted_ = false;
	// An archive that was mounted but never written to leaves no overlay:
	// its directory is removed, rather than accumulating an empty one per
	// game. The lock is taken again (the unmount released it) so that
	// another instance mounting in between keeps its overlay.
	try {
		session_options options = {};
		options.overlay_root    = overlay_root_;
		remove_overlay_if_unused(archive_, options);
	} catch (...) {
		// cleanup is best effort: a leftover empty overlay is harmless
	}
}

overlay_changes session::changes() const
{
	if (!mounted_) {
		return {};
	}
	const auto route = fs::mounts::find(root_);
	return route ? count(route->overlay) : overlay_changes{};
}

std::expected<std::filesystem::path, revise_error> session::revise(const create_options& options)
{
	if (!mounted_) {
		return std::unexpected(revise_error::mount_not_found);
	}
	return revise_archive(root_, options);
}

std::string to_string(clear_error e)
{
	switch (e) {
	case clear_error::archive_in_use:
		return "the archive is in use (mounted by another instance, or a stale .rwz.lock is present)";
	case clear_error::io:
		return "the archive's changes could not be cleared";
	}
	return "unknown error";
}

std::expected<void, clear_error> clear_changes(const std::filesystem::path& archive,
                                               const session_options& options)
{
	const auto overlay_dir = overlay::overlay_directory(
	        archive, options.overlay_root, overlay::process_environment());
	if (!overlay_dir) {
		return std::unexpected(clear_error::io);
	}
	std::error_code ec = {};
	if (!std::filesystem::exists(*overlay_dir, ec)) {
		return {}; // no changes were ever made
	}

	const auto locked = overlay::lock::acquire(*overlay_dir);
	if (!locked) {
		return std::unexpected(locked.error() == overlay::lock::lock_error::in_use
		                               ? clear_error::archive_in_use
		                               : clear_error::io);
	}
	bool ok = true;
	for (const char* area : {"data", "whiteouts", "opaque"}) {
		std::error_code rm_ec = {};
		std::filesystem::remove_all(*overlay_dir / area, rm_ec);
		ok = ok && !rm_ec;
	}
	// Nothing is left of the changes: the overlay directory goes with them
	// (lock included, released below).
	std::error_code rm_ec = {};
	std::filesystem::remove_all(*overlay_dir, rm_ec);
	overlay::lock::release(*overlay_dir);
	if (!ok) {
		return std::unexpected(clear_error::io);
	}
	return {};
}

std::string to_string(extract_error e)
{
	switch (e) {
	case extract_error::source_missing:
		return "the archive does not exist or cannot be read";
	case extract_error::archive_in_use:
		return "the archive is in use (mounted by another instance, or a stale .rwz.lock is present)";
	case extract_error::dest_unavailable:
		return "the destination is not a directory, or cannot be created";
	case extract_error::dest_not_empty:
		return "the destination directory is not empty";
	case extract_error::io:
		return "a file could not be copied (the destination holds a partial copy)";
	}
	return "unknown error";
}

namespace {

// The copy's state: the counts, and the progress callback with its timer.
struct copy_state {
	extract_stats stats                                          = {};
	const std::function<void(const extract_stats&)>* on_progress = nullptr;
	std::chrono::steady_clock::time_point last_report =
	        std::chrono::steady_clock::now();
};

void report_if_due(copy_state& state)
{
	if (state.on_progress == nullptr || !*state.on_progress) {
		return;
	}
	const auto now = std::chrono::steady_clock::now();
	if (now - state.last_report < std::chrono::seconds(2)) {
		return;
	}
	state.last_report = now;
	(*state.on_progress)(state.stats);
}

// Copies one view directory into `dest` (created by the caller). Throws
// std::filesystem::filesystem_error from the view's iteration; the caller
// reports it as io.
bool copy_view_tree(const std::filesystem::path& view_dir,
                    const std::filesystem::path& dest, copy_state& state)
{
	extract_stats& stats = state.stats;
	for (const auto& entry : rwz::fs::directory_iterator(view_dir)) {
		const auto target  = dest / entry.path().filename();
		std::error_code ec = {};
		if (entry.is_symlink(ec)) {
			const auto link = rwz::fs::read_symlink(entry.path(), ec);
			if (ec) {
				return false;
			}
			std::filesystem::create_symlink(link, target, ec);
			if (ec) {
				return false;
			}
			++stats.symlinks;
		} else if (entry.is_directory(ec)) {
			std::filesystem::create_directory(target, ec);
			if (ec) {
				return false;
			}
			++stats.directories;
			if (!copy_view_tree(entry.path(), target, state)) {
				return false;
			}
		} else if (entry.is_regular_file(ec)) {
			std::FILE* in = rwz::stdio::fopen(entry.path().string().c_str(),
			                                  "rb");
			if (in == nullptr) {
				return false;
			}
			std::ofstream out(target, std::ios::binary);
			std::vector<char> buffer(64 * 1024);
			bool ok = out.is_open();
			while (ok) {
				const std::size_t got = rwz::stdio::fread(
				        buffer.data(), 1, buffer.size(), in);
				if (got == 0) {
					break;
				}
				out.write(buffer.data(),
				          static_cast<std::streamsize>(got));
				ok = static_cast<bool>(out);
				stats.bytes += got;
			}
			rwz::stdio::fclose(in);
			if (!ok) {
				return false;
			}
			++stats.files;
			report_if_due(state);
		}
		// other file types (sockets, devices) are not part of an
		// archive's contents: skipped
	}
	return true;
}

} // namespace

std::expected<extract_stats, extract_error> extract_archive(
        const std::filesystem::path& archive, const std::filesystem::path& dest,
        const session_options& options,
        const std::function<void(const extract_stats&)>& on_progress)
{
	auto opened = session::open(archive, options);
	if (!opened) {
		switch (opened.error()) {
		case MountError::ArchiveNotFound:
			return std::unexpected(extract_error::source_missing);
		case MountError::ArchiveInUse:
			return std::unexpected(extract_error::archive_in_use);
		default: return std::unexpected(extract_error::source_missing);
		}
	}

	std::error_code ec = {};
	if (std::filesystem::exists(dest, ec)) {
		if (!std::filesystem::is_directory(dest, ec)) {
			return std::unexpected(extract_error::dest_unavailable);
		}
		if (!std::filesystem::is_empty(dest, ec)) {
			return std::unexpected(extract_error::dest_not_empty);
		}
	} else if (!std::filesystem::create_directories(dest, ec)) {
		return std::unexpected(extract_error::dest_unavailable);
	}

	copy_state state  = {};
	state.on_progress = &on_progress;
	try {
		if (!copy_view_tree(opened->root(), dest, state)) {
			return std::unexpected(extract_error::io);
		}
	} catch (const std::filesystem::filesystem_error&) {
		return std::unexpected(extract_error::io);
	}
	if (on_progress) {
		on_progress(state.stats);
	}
	return state.stats;
}

std::expected<std::filesystem::path, create_error> archive_directory(
        const std::filesystem::path& name, const std::filesystem::path& source,
        const create_options& options)
{
	std::error_code ec = {};
	if (!std::filesystem::is_directory(source, ec)) {
		return std::unexpected(create_error::source_missing);
	}

	std::filesystem::path out = name;
	if (out.extension() != ".dwarfs") {
		out += ".dwarfs";
	}
	if (!out.has_parent_path()) {
		out = std::filesystem::current_path(ec) / out;
	}
	if (std::filesystem::exists(out, ec)) {
		return std::unexpected(create_error::already_exists);
	}

	if (auto made = create_archive(source, out, options); !made) {
		return std::unexpected(made.error());
	}
	return out;
}

} // namespace rwz
