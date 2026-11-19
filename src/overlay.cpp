#include "rwzfs/overlay.h"

#include <cstdlib>
#include <string>

namespace rwz::overlay {

namespace stdfs = std::filesystem;

namespace {

constexpr const char* kAppDir       = "rwz";
constexpr const char* kWritesSuffix = "-writes";

stdfs::path state_relative_to_home()
{
#if defined(__APPLE__)
	return stdfs::path("Library") / "Application Support" / kAppDir;
#else
	return stdfs::path(".local") / "state" / kAppDir;
#endif
}

} // namespace

std::string archive_stem(const stdfs::path& archive)
{
	const auto name = archive.filename().string();
	// Skip index 0 so a leading dot (".hidden") is kept.
	const auto dot = name.find('.', 1);
	if (dot == std::string::npos || dot == 0) {
		return name;
	}
	return name.substr(0, dot);
}

std::expected<stdfs::path, overlay_error> default_write_root(const env_lookup& env)
{
#if defined(_WIN32)
	const auto base = env("LOCALAPPDATA");
	if (!base) {
		return std::unexpected(overlay_error::user_state_unknown);
	}
	return stdfs::path(*base) / kAppDir;
#elif defined(__APPLE__)
	const auto home = env("HOME");
	if (!home) {
		return std::unexpected(overlay_error::user_state_unknown);
	}
	return stdfs::path(*home) / state_relative_to_home();
#else
	// XDG Base Directory spec: XDG_STATE_HOME is used only when it is an
	// absolute path.
	if (const auto xdg = env("XDG_STATE_HOME");
	    xdg && stdfs::path(*xdg).is_absolute()) {
		return stdfs::path(*xdg) / kAppDir;
	}
	const auto home = env("HOME");
	if (!home) {
		return std::unexpected(overlay_error::user_state_unknown);
	}
	return stdfs::path(*home) / state_relative_to_home();
#endif
}

std::expected<stdfs::path, overlay_error> overlay_directory(
        const stdfs::path& archive,
        const std::optional<stdfs::path>& write_root, const env_lookup& env)
{
	stdfs::path root;
	if (write_root) {
		// A relative root is resolved against the current directory,
		// now, so the overlay does not move if the process changes
		// directory later.
		std::error_code ec = {};
		root               = write_root->is_absolute()
		                           ? *write_root
		                           : stdfs::absolute(*write_root, ec);
		if (ec) {
			return std::unexpected(overlay_error::write_root_relative);
		}
	} else {
		auto def = default_write_root(env);
		if (!def) {
			return std::unexpected(def.error());
		}
		root = *def;
	}
	return root / (archive_stem(archive) + kWritesSuffix);
}

env_lookup process_environment()
{
	return [](const char* name) -> std::optional<std::string> {
		if (const char* value = std::getenv(name);
		    value != nullptr && *value != '\0') {
			return std::string(value);
		}
		return std::nullopt;
	};
}

} // namespace rwz::overlay
