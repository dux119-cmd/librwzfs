#include "rwzfs/archive/create_view.h"

#include "rwzfs/fs/mount_table.h"
#include "rwzfs/fs/resolve.h"

#if defined(RWZFS_HAVE_DWARFS)

#include <dwarfs/block_compressor.h>
#include <dwarfs/detail/file_extent_info.h>
#include <dwarfs/detail/file_segment_impl.h>
#include <dwarfs/detail/file_view_impl.h>
#include <dwarfs/extent_kind.h>
#include <dwarfs/file_access.h>
#include <dwarfs/file_access_generic.h>
#include <dwarfs/file_extents_iterable.h>
#include <dwarfs/file_range.h>
#include <dwarfs/file_segment.h>
#include <dwarfs/file_stat.h>
#include <dwarfs/file_view.h>
#include <dwarfs/fstypes.h>
#include <dwarfs/logger.h>
#include <dwarfs/os_access.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/thread_pool.h>
#include <dwarfs/writer/categorizer.h>
#include <dwarfs/writer/entry_factory.h>
#include <dwarfs/writer/filesystem_writer.h>
#include <dwarfs/writer/filesystem_writer_options.h>
#include <dwarfs/writer/scanner.h>
#include <dwarfs/writer/scanner_options.h>
#include <dwarfs/writer/segmenter.h>
#include <dwarfs/writer/segmenter_factory.h>
#include <dwarfs/writer/writer_progress.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <boost/program_options.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#endif

#include "rwzfs/fs/archive_path.h"
#include "rwzfs/fs/filesystem.h"
#include "rwzfs/fs/mount_table.h"
#include "rwzfs/fs/resolve.h"
#if defined(_WIN32)
#include "rwzfs/io/win32.h"
#else
#include "rwzfs/io/posix.h"
#endif
#include "rwzfs/overlay/cow.h"

namespace rwz {

namespace {

namespace dw  = dwarfs;
namespace fs  = rwz::fs;
namespace cow = rwz::overlay::cow;
#if !defined(_WIN32)
namespace posix = rwz::posix;
#endif

constexpr dw::file_size_t kDefaultSegmentSize = 1 << 20;

// Reads exactly `count` bytes at `offset` from an archive file. Throws on error
// or short read.
void read_exact(const rwz::archive::backend& be, std::uint64_t inode,
                std::uint64_t offset, char* dest, std::size_t count)
{
	std::size_t done = 0;
	while (done < count) {
		std::error_code ec;
		const auto got = be.read(
		        inode, dest + done, count - done, offset + done, ec);
		if (ec) {
			throw std::system_error(ec, "archive read");
		}
		if (got == 0) {
			throw std::runtime_error("archive read: unexpected end of file");
		}
		done += got;
	}
}

// A segment of an archive file: owns its bytes.
class archive_segment final : public dw::detail::file_segment_impl {
public:
	archive_segment(dw::file_off_t offset, std::vector<std::byte> data)
	        : offset_{offset},
	          data_{std::move(data)}
	{}

	dw::file_off_t offset() const noexcept override
	{
		return offset_;
	}
	dw::file_size_t size() const noexcept override
	{
		return static_cast<dw::file_size_t>(data_.size());
	}
	dw::file_range range() const noexcept override
	{
		return dw::file_range{offset_, size()};
	}
	bool is_zero() const noexcept override
	{
		return std::all_of(data_.begin(), data_.end(), [](std::byte b) {
			return b == std::byte{0};
		});
	}
	std::span<const std::byte> raw_bytes() const override
	{
		return data_;
	}
	void advise(dw::io_advice, std::error_code&) const override {}
	void lock(std::error_code&) const override {}

private:
	dw::file_off_t offset_{};
	std::vector<std::byte> data_{};
};

// Contents of one archive-only file, read through the backend. The file is a
// single data extent.
class archive_view final : public dw::detail::file_view_impl,
                           public std::enable_shared_from_this<archive_view> {
public:
	archive_view(std::shared_ptr<const rwz::archive::backend> be,
	             std::uint64_t inode, dw::file_size_t size,
	             std::filesystem::path path)
	        : be_{std::move(be)},
	          inode_{inode},
	          size_{size},
	          path_{std::move(path)}
	{
		if (size_ > 0) {
			extents_.emplace_back(dw::extent_kind::data,
			                      dw::file_range{0, size_});
		}
	}

	dw::file_size_t size() const override
	{
		return size_;
	}
	const std::filesystem::path& path() const override
	{
		return path_;
	}

	dw::file_segment segment_at(dw::file_range range) const override
	{
		std::vector<std::byte> data(static_cast<std::size_t>(range.size()));
		read_exact(*be_,
		           inode_,
		           static_cast<std::uint64_t>(range.offset()),
		           reinterpret_cast<char*>(data.data()),
		           data.size());
		return dw::file_segment(
		        std::make_shared<archive_segment>(range.offset(),
			                                  std::move(data)));
	}

	dw::file_extents_iterable extents(std::optional<dw::file_range> range) const override
	{
		return dw::file_extents_iterable(shared_from_this(),
		                                 extents_,
		                                 range.value_or(
		                                         dw::file_range{0, size_}));
	}

	bool supports_raw_bytes() const noexcept override
	{
		return false;
	}
	std::span<const std::byte> raw_bytes() const override
	{
		return {};
	}

	void copy_bytes(void* dest, dw::file_range range, std::error_code& ec) const override
	{
		try {
			read_exact(*be_,
			           inode_,
			           static_cast<std::uint64_t>(range.offset()),
			           static_cast<char*>(dest),
			           static_cast<std::size_t>(range.size()));
		} catch (const std::system_error& e) {
			ec = e.code();
		} catch (const std::exception&) {
			ec = std::make_error_code(std::errc::io_error);
		}
	}

	size_t default_segment_size() const override
	{
		return kDefaultSegmentSize;
	}
	void release_until(dw::file_off_t, std::error_code&) const override {}

private:
	std::shared_ptr<const rwz::archive::backend> be_{};
	std::uint64_t inode_{};
	dw::file_size_t size_{};
	std::filesystem::path path_{};
	std::vector<dw::detail::file_extent_info> extents_{};
};

// Where a path lives in the merged view: the mount it routes to and the entry
// it names.
struct located {
	cow::node node;
	cow::layer layer;
};

std::optional<located> locate(const std::filesystem::path& path)
{
	const auto p = rwz::fs::resolve_lexical(path);
	const auto r = rwz::fs::mounts::find(p);
	if (!r) {
		return std::nullopt;
	}
	const auto l = cow::layer_of(*r);
	const auto n = cow::lookup(l);
	if (!n) {
		return std::nullopt;
	}
	return located{*n, l};
}

// Directory listing (merged view). The scanner expects each name as a full path
// (directory plus entry name), since it passes that path to symlink_info. POSIX
// uses the library's opendir/readdir; Windows uses its
// FindFirstFileW/FindNextFileW, which list archive directories the same way.
#if defined(_WIN32)
class view_dir_reader final : public dw::dir_reader {
public:
	view_dir_reader(void* find, WIN32_FIND_DATAW first,
	                std::filesystem::path dir_path)
	        : find_{find},
	          pending_{first},
	          pending_valid_{true},
	          dir_path_{std::move(dir_path)}
	{}
	~view_dir_reader() override
	{
		if (find_ != INVALID_HANDLE_VALUE) {
			(void)rwz::win32::find_close(find_);
		}
	}
	bool read(std::filesystem::path& name) override
	{
		for (;;) {
			if (!pending_valid_) {
				if (!rwz::win32::find_next_file(find_, &pending_)) {
					return false;
				}
			}
			pending_valid_           = false;
			const std::wstring entry = pending_.cFileName;
			if (entry == L"." || entry == L"..") {
				continue;
			}
			name = dir_path_ / std::filesystem::path(entry);
			return true;
		}
	}

private:
	void* find_;
	WIN32_FIND_DATAW pending_{};
	bool pending_valid_{};
	std::filesystem::path dir_path_{};
};
#else
class view_dir_reader final : public dw::dir_reader {
public:
	view_dir_reader(DIR* dir, std::filesystem::path dir_path)
	        : dir_{dir},
	          dir_path_{std::move(dir_path)}
	{}
	~view_dir_reader() override
	{
		if (dir_ != nullptr) {
			posix::closedir(dir_);
		}
	}
	bool read(std::filesystem::path& name) override
	{
		while (const struct dirent* e = posix::readdir(dir_)) {
			const std::string_view n{e->d_name};
			if (n == "." || n == "..") {
				continue;
			}
			name = dir_path_ / std::string(n);
			return true;
		}
		return false;
	}

private:
	DIR* dir_;
	std::filesystem::path dir_path_{};
};
#endif

// os_access for the scanner: directories, metadata, and contents come from the
// merged view. Host paths (no mount) fall through to the generic implementation.
class view_os_access final : public dw::os_access_generic {
public:
	std::unique_ptr<dw::dir_reader> opendir(const std::filesystem::path& path) const override
	{
#if defined(_WIN32)
		// Every directory listing starts with "." and "..", so the
		// first record is always there.
		WIN32_FIND_DATAW first{};
		const auto pattern = (path / L"*").wstring();
		void* find = rwz::win32::find_first_file(pattern.c_str(), &first);
		if (find == INVALID_HANDLE_VALUE) {
			throw std::system_error(static_cast<int>(GetLastError()),
			                        std::system_category(),
			                        path.string());
		}
		return std::make_unique<view_dir_reader>(find, first, path);
#else
		DIR* dir = posix::opendir(path.string().c_str());
		if (dir == nullptr) {
			throw std::system_error(errno,
			                        std::generic_category(),
			                        path.string());
		}
		return std::make_unique<view_dir_reader>(dir, path);
#endif
	}

	dw::file_stat symlink_info(const std::filesystem::path& path) const override
	{
		const auto loc = locate(path);
		if (!loc) {
			return dw::os_access_generic::symlink_info(path);
		}
		if (loc->node.where == cow::origin::overlay) {
			auto st = dw::os_access_generic::symlink_info(loc->node.host);
			st.set_uid(0); // no owners in the archive, whatever the
			               // host file's owner is
			st.set_gid(0);
			return st;
		}
		// The writer requires every field to be valid, so fill them
		// all. Archive entries carry mode, size, time and inode; the
		// rest take the values a regular archive entry would have.
		const auto& e = loc->node.info;
		const auto mtime = static_cast<dw::file_stat::time_type>(e.mtime);
		const auto size = static_cast<dw::file_size_t>(e.size);
		dw::file_stat st;
		st.set_dev(0);
		st.set_ino(e.inode);
		st.set_nlink(1);
		st.set_mode(e.mode);
		st.set_uid(0); // the archive stores no owners (see docs/USAGE.md)
		st.set_gid(0);
		st.set_rdev(0);
		st.set_size(size);
		st.set_blksize(4096);
		st.set_blocks((size + 511) / 512);
		st.set_allocated_size(size);
		st.set_atime(mtime);
		st.set_mtime(mtime);
		st.set_ctime(mtime);
		return st;
	}

	std::filesystem::path read_symlink(const std::filesystem::path& path) const override
	{
		const auto loc = locate(path);
		if (!loc || loc->node.where == cow::origin::overlay) {
			return dw::os_access_generic::read_symlink(
			        loc ? loc->node.host : path);
		}
		std::error_code ec;
		auto target = rwz::fs::read_symlink(path, ec);
		if (ec) {
			throw std::system_error(ec, path.string());
		}
		return target;
	}

	dw::file_view open_file(const std::filesystem::path& path) const override
	{
		const auto loc = locate(path);
		if (!loc) {
			return dw::os_access_generic::open_file(path);
		}
		if (loc->node.where == cow::origin::overlay) {
			return dw::os_access_generic::open_file(loc->node.host);
		}
		return dw::file_view(std::make_shared<archive_view>(
		        loc->layer.backend,
		        loc->node.info.inode,
		        static_cast<dw::file_size_t>(loc->node.info.size),
		        path));
	}

	dw::file_view open_file_with_options(const std::filesystem::path& path,
	                                     const dw::open_file_options& opts) const override
	{
		const auto loc = locate(path);
		if (!loc || loc->node.where == cow::origin::overlay) {
			return dw::os_access_generic::open_file_with_options(
			        loc ? loc->node.host : path, opts);
		}
		return open_file(path);
	}

	int access(const std::filesystem::path& path, int mode) const override
	{
#if defined(_WIN32)
		// Windows has no POSIX permission bits; the scanner only needs
		// to know the entry is there.
		(void)mode;
		// The library's own check: std::filesystem would look only at
		// the host disk, and archive paths are answered from the merged
		// view.
		std::error_code ec;
		const bool present = rwz::fs::exists(path, ec);
		return (present && !ec) ? 0 : -1;
#else
		return posix::access(path.string().c_str(), mode);
#endif
	}

	std::filesystem::path canonical(const std::filesystem::path& path) const override
	{
		return path;
	}
};

// Removes the temporary output and reports an error.
std::expected<void, create_error> fail(const std::filesystem::path& tmp,
                                       create_error e)
{
	std::error_code ec;
	std::filesystem::remove(tmp, ec);
	return std::unexpected(e);
}

} // namespace

// Writes the archive of the tree at `root`. Every path the writer sees goes
// through view_os_access, which routes mounted paths to the merged view and
// leaves all other paths to the host.
std::expected<void, create_error> write_archive(const std::filesystem::path& root,
                                                const std::filesystem::path& output,
                                                const create_options& options,
                                                std::ostream* log)
{
	auto tmp = output;
	tmp += ".tmp";
	{
		std::ofstream probe(tmp, std::ios::binary | std::ios::trunc);
		if (!probe) {
			return std::unexpected(create_error::io);
		}
	}
	std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
	if (!os) {
		return std::unexpected(create_error::io);
	}

	try {
		view_os_access access;
		dw::null_logger silent;
		std::optional<dw::stream_logger> diag;
		if (log != nullptr) {
			diag.emplace(*log, access);
		}
		dw::logger& lgr = diag ? static_cast<dw::logger&>(*diag)
		                       : static_cast<dw::logger&>(silent);
		dw::writer::writer_progress prog([](dw::writer::writer_progress&,
		                                    bool) {},
		                                 std::chrono::milliseconds(100));
		const auto workers = static_cast<std::size_t>(
		        (std::max)(1u, std::thread::hardware_concurrency()));

		// --categorize: the default categorizers mkdwarfs uses when
		// given no list.
		auto catmgr = std::make_shared<dw::writer::categorizer_manager>(lgr, root);
		const auto fa = std::shared_ptr<const dw::file_access>(
		        dw::create_file_access_generic());
		dw::writer::categorizer_registry catreg;
		// Every categorizer option takes its default: register the
		// options, parse no arguments.
		boost::program_options::options_description cat_opts("categorizers");
		catreg.add_options(cat_opts);
		boost::program_options::variables_map no_options;
		boost::program_options::store(boost::program_options::command_line_parser(
		                                      std::vector<std::string>{})
		                                      .options(cat_opts)
		                                      .run(),
		                              no_options);
		boost::program_options::notify(no_options);
		for (const char* name :
		     {"fits", "pcmaudio", "binary", "incompressible"}) {
			catmgr->add(std::shared_ptr<dw::writer::categorizer>(
			        catreg.create(lgr, name, no_options, fa)));
		}

		// Writer and segmenter settings. The per-category defaults are
		// the ones mkdwarfs uses for its default level; without them
		// the segmenter factory reads an empty optional.
		dw::writer::segmenter_factory::config cfg;
		const dw::writer::segmenter::config seg_defaults{};
		cfg.blockhash_window_size.set_default(
		        seg_defaults.blockhash_window_size);
		cfg.window_increment_shift.set_default(
		        seg_defaults.window_increment_shift);
		cfg.max_active_blocks.set_default(seg_defaults.max_active_blocks);
		cfg.bloom_filter_size.set_default(seg_defaults.bloom_filter_size);

		dw::thread_pool compress_pool(lgr, access, "compress", workers);
		dw::writer::filesystem_writer_options fswopts;
		dw::writer::filesystem_writer fsw(os, lgr, compress_pool, prog, fswopts);
		// --compression: applies to every block (the categorizers pick
		// no separate compressor).
		fsw.add_default_compressor(dw::block_compressor(options.compression));
		// Metadata sections use zstd. No history section: --no-history.
		fsw.add_section_compressor(dw::section_type::METADATA_V2_SCHEMA,
		                           dw::block_compressor("zstd"));
		fsw.add_section_compressor(dw::section_type::METADATA_V2,
		                           dw::block_compressor("zstd"));

		dw::writer::segmenter_factory sf(lgr, prog, catmgr, cfg);
		dw::writer::entry_factory ef;
		dw::thread_pool scanner_pool(lgr, access, "scanner", workers);
		dw::writer::scanner_options sopts;
		sopts.enable_history        = false; // --no-history
		sopts.inode.categorizer_mgr = catmgr;
		dw::writer::scanner scanner(lgr, scanner_pool, sf, ef, access, sopts);
		scanner.scan(fsw, root, prog);
	} catch (const std::exception& e) {
		if (log != nullptr) {
			*log << "create_archive_from_view: " << e.what() << "\n";
		}
		os.close();
		return fail(tmp, create_error::tool_failed);
	}

	os.close();
	if (!os) {
		return fail(tmp, create_error::io);
	}
	std::error_code ec;
	std::filesystem::rename(tmp, output, ec);
	if (ec) {
		return fail(tmp, create_error::io);
	}
	return {};
}

std::expected<void, create_error> create_archive_from_view(
        const std::filesystem::path& root, const std::filesystem::path& output,
        const create_options& options, std::ostream* log)
{
	if (!rwz::fs::mounts::find(rwz::fs::resolve_lexical(root))) {
		return std::unexpected(create_error::source_missing);
	}
	return write_archive(root, output, options, log);
}

std::expected<void, create_error> create_archive(const std::filesystem::path& source_dir,
                                                 const std::filesystem::path& output,
                                                 const create_options& options)
{
	std::error_code ec;
	if (!std::filesystem::is_directory(source_dir, ec)) {
		return std::unexpected(create_error::source_missing);
	}
	return write_archive(source_dir, output, options, nullptr);
}

} // namespace rwz

#else // !RWZFS_HAVE_DWARFS

namespace rwz {

std::expected<void, create_error> create_archive_from_view(
        const std::filesystem::path& root, const std::filesystem::path&,
        const create_options&, std::ostream*)
{
	if (!rwz::fs::mounts::find(rwz::fs::resolve_lexical(root))) {
		return std::unexpected(create_error::source_missing);
	}
	return std::unexpected(create_error::unsupported_platform);
}

std::expected<void, create_error> create_archive(const std::filesystem::path& source_dir,
                                                 const std::filesystem::path&,
                                                 const create_options&)
{
	std::error_code ec;
	if (!std::filesystem::is_directory(source_dir, ec)) {
		return std::unexpected(create_error::source_missing);
	}
	return std::unexpected(create_error::unsupported_platform);
}

} // namespace rwz

#endif // RWZFS_HAVE_DWARFS
