#include "rwzfs/fs/resolve.h"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "rwzfs/archive/backend.h"
#include "rwzfs/fs/archive_path.h"
#include "rwzfs/fs/cwd.h"
#include "rwzfs/fs/mount_table.h"
#include "rwzfs/fs/walk.h"
#include "rwzfs/overlay/cow.h"

namespace rwz::fs {

namespace stdfs = std::filesystem;

namespace {

// Target text if `candidate` (absolute, inside the archive) is a symlink.
std::optional<std::string> archive_link_target(const std::vector<std::string>& candidate)
{
	const auto p = detail::absolute_from(candidate, 0);
	const auto r = mounts::find(p);
	if (!r || !r->backend) {
		return std::nullopt;
	}
	// The merged view decides: an overlay file shadows an archive symlink,
	// and overlay entries are never links.
	const auto n = rwz::overlay::cow::lookup(rwz::overlay::cow::layer_of(*r));
	if (!n || n->where != rwz::overlay::cow::origin::archive ||
	    n->info.type != rwz::archive::kind::symlink) {
		return std::nullopt;
	}
	return r->backend->link_target(r->rel);
}

// Walks an absolute path one component at a time, so a symlink in the middle of
// the path is followed before the next component (as the OS does, so "link/.."
// is correct). The mount that owns each position decides how it is walked: inside
// a mount, symlinks are followed in the archive, and ".." climbs out of the
// mount's root to its real parent, which may be the host or another mount. Host
// paths are passed on raw, so the OS resolves their symlinks.
stdfs::path resolve_components(const stdfs::path& input, std::error_code& ec,
                               bool follow_final)
{
	std::deque<std::string> pending;
	for (auto& c : detail::components(input.generic_string())) {
		pending.push_back(c);
	}

	std::vector<std::string> out;
	int follows = 0;
	while (!pending.empty()) {
		std::string token = std::move(pending.front());
		pending.pop_front();
		if (token == ".") {
			continue;
		}

		const auto here = mounts::find(detail::absolute_from(out, 0));
		const auto root = here ? detail::components(
		                                 here->mountpoint.generic_string())
		                       : std::vector<std::string>{};

		if (token == "..") {
			if (here && out.size() > root.size()) {
				out.pop_back(); // inside the mount: a normal climb
				continue;
			}
			if (here) {
				// At the mount's root: climb out of the mount.
				// The parent may be another mount, which is
				// walked from there.
				out.pop_back();
				if (mounts::find(detail::absolute_from(out, 0))) {
					continue;
				}
				out.insert(out.end(), pending.begin(), pending.end());
				ec.clear();
				return detail::absolute_from(out, 0); // the host
				                                      // path, raw
			}
			// On the host: a ".." can lead back into a mount (host
			// "/a/b/x/../c/file"). If the normalized path is in
			// one, walk that instead; otherwise the rest is a host
			// path, passed raw.
			std::vector<std::string> full = out;
			full.push_back(token);
			full.insert(full.end(), pending.begin(), pending.end());
			const auto lexical = walk_lexically(
			        detail::absolute_from(full, 0));
			if (mounts::find(lexical)) {
				out.clear();
				pending.clear();
				for (auto& c :
				     detail::components(lexical.generic_string())) {
					pending.push_back(c);
				}
				continue;
			}
			out.push_back(token);
			out.insert(out.end(), pending.begin(), pending.end());
			ec.clear();
			return detail::absolute_from(out, 0);
		}

		auto candidate = out;
		candidate.push_back(token);
		const bool is_final = pending.empty();
		bool inside_archive = false;
		if (const auto owner = mounts::find(
		            detail::absolute_from(candidate, 0));
		    owner && owner->backend) {
			const auto mp = detail::components(
			        owner->mountpoint.generic_string());
			inside_archive = detail::has_prefix(candidate, mp) &&
			                 candidate.size() > mp.size();
		}
		if (!inside_archive || (is_final && !follow_final)) {
			out.push_back(token);
			continue;
		}

		const auto target = archive_link_target(candidate);
		if (!target) {
			out.push_back(token);
			continue;
		}
		if (++follows > kMaxSymlinkFollows) {
			ec = std::make_error_code(
			        std::errc::too_many_symbolic_link_levels);
			return {};
		}
		// An absolute target restarts the walk from "/". A relative one
		// continues from this directory.
		if (stdfs::path(*target).is_absolute()) {
			out.clear();
		}
		const auto parts = detail::components(*target);
		for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
			pending.push_front(*it);
		}
	}
	ec.clear();
	return detail::absolute_from(out, 0);
}

stdfs::path resolve_with(const stdfs::path& p, std::error_code& ec, bool follow_final)
{
	if (p.is_absolute()) {
		return resolve_components(p, ec, follow_final);
	}
	const auto base = cwd::archive_cwd();
	if (!base) {
		ec.clear();
		return p; // relative host path: the OS resolves it against the
		          // host working directory
	}
	return resolve_components(*base / p, ec, follow_final);
}

} // namespace

stdfs::path resolve_lexical(const stdfs::path& p)
{
	if (p.is_absolute()) {
		return walk_lexically(p);
	}
	const auto base = cwd::archive_cwd();
	if (!base) {
		return p;
	}
	return walk_lexically(*base / p);
}

stdfs::path resolve(const stdfs::path& p, std::error_code& ec)
{
	return resolve_with(p, ec, true);
}

stdfs::path resolve_no_follow(const stdfs::path& p, std::error_code& ec)
{
	return resolve_with(p, ec, false);
}

} // namespace rwz::fs
