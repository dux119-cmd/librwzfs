#include "rwzfs/session.h"

#include <system_error>
#include <utility>

#include "rwzfs/fs/archive_path.h"
#include "rwzfs/fs/mount_table.h"

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
	s.mounted_ = true;
	return s;
}

session::session(std::filesystem::path root, std::filesystem::path archive)
        : root_(std::move(root)),
          archive_(std::move(archive))
{}

session::session(session&& other) noexcept
        : root_(std::move(other.root_)),
          archive_(std::move(other.archive_)),
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
	if (mounted_) {
		unmount_archive(root_);
		mounted_ = false;
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
