#include "rwzfs/io/stdio.h"

#include <sys/types.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "rwzfs/archive/backend.h"
#include "rwzfs/fs/cwd.h"
#include "rwzfs/fs/mount_table.h"
#include "rwzfs/fs/resolve.h"
#include "rwzfs/overlay/cow.h"

namespace rwz::stdio {

namespace fs  = rwz::fs;
namespace cow = rwz::overlay::cow;
using rwz::fs::mounts::route;

namespace {

constexpr std::size_t kStreamBufferBytes = 64 * 1024; // read chunk for C++
                                                      // input streams

// Resolves a caller path (CWD-05). On failure sets errno (for example ELOOP)
// and returns nullopt.
std::optional<fs::path> resolved(const std::string& text, bool follow_final = true)
{
	std::error_code ec;
	const auto p = follow_final ? rwz::fs::resolve(fs::path(text), ec)
	                            : fs::resolve_no_follow(fs::path(text), ec);
	if (ec) {
		errno = ec.value();
		return std::nullopt;
	}
	return p;
}

bool has_write_intent(const char* mode)
{
	for (const char* c = mode; *c != '\0'; ++c) {
		if (*c == 'w' || *c == 'a' || *c == '+') {
			return true;
		}
	}
	return false;
}

// Reads the whole archive file into memory. Returns false with errno set on
// failure.

std::FILE* open_lazy(const cow::layer& l, const char* mode,
                     const rwz::archive::entry& info);

#if defined(_WIN32)
// Windows has no callback stream, so a read-only archive file is copied, in
// chunks, to a temporary file that is deleted when the stream is closed. The
// copy is opened read-only after it is written, so writes to it fail as they do
// on POSIX. Windows keeps recent temporary files in the page cache.
constexpr std::uint64_t kCopyChunkBytes = 1024 * 1024;

std::mutex& copy_mutex()
{
	static std::mutex m;
	return m;
}

std::map<std::FILE*, std::filesystem::path>& copies()
{
	static std::map<std::FILE*, std::filesystem::path> m;
	return m;
}

// mode is the caller's read mode ("r" or "rb"): text mode translates CRLF
// exactly as on a host file.
std::FILE* open_archive_copy(const route& r, const rwz::archive::entry& e,
                             const char* mode)
{
	std::error_code temp_ec{};
	const auto dir = std::filesystem::temp_directory_path(temp_ec);
	if (temp_ec) {
		errno = temp_ec.value();
		return nullptr;
	}
	const auto path = dir / ("rwz-" + std::to_string(std::random_device{}()) +
	                         ".tmp");

	auto fail = [&](int err, std::FILE* out) -> std::FILE* {
		if (out != nullptr) {
			std::fclose(out);
		}
		std::error_code rm_ec{};
		std::filesystem::remove(path, rm_ec);
		errno = err;
		return nullptr;
	};

	std::FILE* out = std::fopen(path.string().c_str(), "wb");
	if (out == nullptr) {
		return nullptr; // errno is set by fopen
	}

	std::vector<char> chunk(static_cast<std::size_t>(kCopyChunkBytes), 0);
	std::uint64_t offset = 0;
	while (offset < e.size) {
		const auto want = std::min<std::uint64_t>(kCopyChunkBytes,
		                                          e.size - offset);
		std::error_code read_ec{};
		const auto got = r.backend->read(e.inode,
		                                 chunk.data(),
		                                 static_cast<std::size_t>(want),
		                                 offset,
		                                 read_ec);
		if (read_ec) {
			return fail(read_ec.value(), out);
		}
		if (got == 0) {
			return fail(EIO, out); // the archive ended before its
			                       // recorded size
		}
		if (std::fwrite(chunk.data(), 1, got, out) != got) {
			return fail(errno != 0 ? errno : EIO, out);
		}
		offset += got;
	}
	if (std::fclose(out) != 0) {
		return fail(errno != 0 ? errno : EIO, nullptr);
	}

	// Reopen with the caller's read mode: it is read-only, so writes to it
	// fail as on POSIX.
	std::FILE* in = std::fopen(path.string().c_str(), mode);
	if (in == nullptr) {
		std::error_code rm_ec{};
		std::filesystem::remove(path, rm_ec);
		return nullptr; // errno is set by fopen
	}
	const std::scoped_lock lock(copy_mutex());
	copies().emplace(in, path);
	return in;
}
#endif // _WIN32

std::FILE* open_archive_stream(const route& r, const char* mode)
{
	if (!r.backend) {
		errno = ENOTSUP;
		return nullptr;
	}
	const auto e = r.backend->lookup(r.rel);
	if (!e) {
		errno = ENOENT;
		return nullptr;
	}
	if (e->type == rwz::archive::kind::directory) {
		errno = EISDIR;
		return nullptr;
	}
	if (e->type != rwz::archive::kind::file) {
		errno = ENOTSUP;
		return nullptr;
	}
	// Chunk reads from the archive through the callback stream (see
	// open_lazy): nothing is copied, and the reads go through dwarfs's
	// block cache. Windows has no callback stream: use CreateFileW.
#if defined(_WIN32)
	return open_archive_copy(r, *e, mode);
#else
	return open_lazy(cow::layer_of(r), mode, *e);
#endif
}

} // namespace

// STDIO-01
std::size_t fread(void* buf, std::size_t size, std::size_t count, std::FILE* stream)
{
	return std::fread(buf, size, count, stream);
}

// STDIO-02
namespace {

bool mode_has(const char* mode, char c)
{
	for (const char* m = mode; *m != '\0'; ++m) {
		if (*m == c) {
			return true;
		}
	}
	return false;
}

// ---- lazy copy-up for write-opens of archive files ------------------------
//
// A write-open of an archive-only file (r+, a, +) must not copy the file up at
// open: the copy happens on the first write. A FILE* has no hook on writes, so
// the stream is built on fopencookie (glibc) or funopen (BSD, macOS), whose
// callbacks do the work. Windows has neither; there the copy happens at open.

#if defined(__GLIBC__) || defined(__APPLE__) || defined(__FreeBSD__)
#define RWZFS_LAZY_STDIO 1
#endif

struct lazy_state {
	cow::layer l;
	bool readable              = false;
	bool writable              = false;
	bool append                = false;
	std::uint64_t offset       = 0;
	std::uint64_t archive_size = 0;
	std::uint64_t inode        = 0;
	std::FILE* host = nullptr; // overlay copy, once the file has been
	                           // copied up
};

std::error_code errc_of(int e)
{
	return std::error_code(e, std::generic_category());
}

// Copies the file up (once) and opens the overlay copy, positioned at the
// stream's offset.
std::error_code materialize_lazy(lazy_state& st)
{
	if (st.host != nullptr) {
		return {};
	}
	const auto path = cow::materialize(st.l);
	if (!path) {
		return path.error();
	}
	st.host = std::fopen(path->string().c_str(), "r+b");
	if (st.host == nullptr) {
		return errc_of(errno);
	}
	if (!st.append &&
	    std::fseek(st.host, static_cast<long>(st.offset), SEEK_SET) != 0) {
		return errc_of(errno);
	}
	return {};
}

// Repositions the stream. Returns the new offset, or nullopt with errno set.
std::optional<std::uint64_t> seek_lazy(lazy_state& st, long long offset, int whence)
{
	long long base = 0;
	switch (whence) {
	case SEEK_SET: base = 0; break;
	case SEEK_CUR: base = static_cast<long long>(st.offset); break;
	case SEEK_END:
		if (st.host != nullptr) {
			if (std::fseek(st.host, 0, SEEK_END) != 0) {
				return std::nullopt;
			}
			const long end = std::ftell(st.host);
			if (end < 0) {
				return std::nullopt;
			}
			base = end;
		} else {
			base = static_cast<long long>(st.archive_size);
		}
		break;
	default: errno = EINVAL; return std::nullopt;
	}
	const long long target = base + offset;
	if (target < 0) {
		errno = EINVAL;
		return std::nullopt;
	}
	st.offset = static_cast<std::uint64_t>(target);
	if (st.host != nullptr &&
	    std::fseek(st.host, static_cast<long>(target), SEEK_SET) != 0) {
		return std::nullopt;
	}
	return st.offset;
}

// Reads come from the archive until the file is copied up; then from the
// overlay copy.
long lazy_read_impl(lazy_state& st, char* buf, std::size_t n)
{
	if (!st.readable) {
		errno = EBADF;
		return -1;
	}
	if (st.host != nullptr) {
		const auto got = std::fread(buf, 1, n, st.host);
		st.offset += got;
		return static_cast<long>(got);
	}
	if (st.offset >= st.archive_size) {
		return 0;
	}
	const auto want = std::min<std::uint64_t>(n, st.archive_size - st.offset);
	std::error_code ec;
	const auto got = st.l.backend->read(
	        st.inode, buf, static_cast<std::size_t>(want), st.offset, ec);
	if (ec) {
		errno = ec.value();
		return -1;
	}
	st.offset += got;
	return static_cast<long>(got);
}

// The first write copies the file up. Returns the number of bytes written, or
// -1 with errno.
long lazy_write_impl(lazy_state& st, const char* buf, std::size_t n)
{
	if (!st.writable) {
		errno = EBADF;
		return -1;
	}
	if (const auto err = materialize_lazy(st)) {
		errno = err.value();
		return -1;
	}
	if (st.append && std::fseek(st.host, 0, SEEK_END) != 0) {
		return -1;
	}
	const auto put = std::fwrite(buf, 1, n, st.host);
	const long pos = std::ftell(st.host);
	if (pos >= 0) {
		st.offset = static_cast<std::uint64_t>(pos);
	}
	if (put == 0 && n > 0) {
		errno = EIO;
		return -1;
	}
	return static_cast<long>(put);
}

void lazy_close_impl(lazy_state* st)
{
	if (st->host != nullptr) {
		std::fclose(st->host);
	}
	delete st;
}

#if defined(__GLIBC__)
ssize_t cookie_read(void* c, char* buf, size_t n)
{
	return lazy_read_impl(*static_cast<lazy_state*>(c), buf, n);
}
ssize_t cookie_write(void* c, const char* buf, size_t n)
{
	return lazy_write_impl(*static_cast<lazy_state*>(c), buf, n);
}
int cookie_seek(void* c, off64_t* off, int whence)
{
	const auto r = seek_lazy(*static_cast<lazy_state*>(c),
	                         static_cast<long long>(*off),
	                         whence);
	if (!r) {
		return -1;
	}
	*off = static_cast<off64_t>(*r);
	return 0;
}
int cookie_close(void* c)
{
	lazy_close_impl(static_cast<lazy_state*>(c));
	return 0;
}
#elif defined(__APPLE__) || defined(__FreeBSD__)
int funopen_read(void* c, char* buf, int n)
{
	return static_cast<int>(lazy_read_impl(*static_cast<lazy_state*>(c),
	                                       buf,
	                                       static_cast<std::size_t>(n)));
}
int funopen_write(void* c, const char* buf, int n)
{
	return static_cast<int>(lazy_write_impl(*static_cast<lazy_state*>(c),
	                                        buf,
	                                        static_cast<std::size_t>(n)));
}
fpos_t funopen_seek(void* c, fpos_t off, int whence)
{
	const auto r = seek_lazy(*static_cast<lazy_state*>(c),
	                         static_cast<long long>(off),
	                         whence);
	return r ? static_cast<fpos_t>(*r) : static_cast<fpos_t>(-1);
}
int funopen_close(void* c)
{
	lazy_close_impl(static_cast<lazy_state*>(c));
	return 0;
}
#endif

// Opens a lazy stream for an archive-only file. Returns nullptr when the
// platform has no callback stream (the caller then copies up at open).
std::FILE* open_lazy(const cow::layer& l, const char* mode,
                     const rwz::archive::entry& info)
{
#if defined(RWZFS_LAZY_STDIO)
	auto* st         = new lazy_state;
	st->l            = l;
	st->readable     = mode_has(mode, 'r') || mode_has(mode, '+');
	st->writable     = mode_has(mode, 'w') || mode_has(mode, 'a') ||
	                   mode_has(mode, '+');
	st->append       = mode_has(mode, 'a');
	st->archive_size = info.size;
	st->inode        = info.inode;
#if defined(__GLIBC__)
	cookie_io_functions_t fns{};
	fns.read     = cookie_read;
	fns.write    = cookie_write;
	fns.seek     = cookie_seek;
	fns.close    = cookie_close;
	std::FILE* f = fopencookie(st, mode, fns);
#else
	std::FILE* f = funopen(st,
	                       st->readable ? funopen_read : nullptr,
	                       st->writable ? funopen_write : nullptr,
	                       funopen_seek,
	                       funopen_close);
#endif
	if (f == nullptr) {
		delete st;
	}
	return f;
#else
	(void)l;
	(void)mode;
	(void)info;
	errno = ENOTSUP; // no callback stream on this platform (Windows)
	return nullptr;
#endif
}

// Routes with an overlay: reads see the merged view; writes go to the overlay
// file.
std::FILE* open_overlay_stream(const route& r, const char* mode)
{
	const auto l = cow::layer_of(r);
	if (!has_write_intent(mode)) {
		const auto n = cow::lookup(l);
		if (!n) {
			errno = ENOENT;
			return nullptr;
		}
		if (n->where == cow::origin::overlay) {
			return std::fopen(n->host.string().c_str(), mode);
		}
		return open_archive_stream(r, mode);
	}
	const bool create   = mode_has(mode, 'w') || mode_has(mode, 'a');
	const bool truncate = mode_has(mode, 'w');
	const auto n        = cow::lookup(l);
	if (n && n->info.type == rwz::archive::kind::directory) {
		errno = EISDIR;
		return nullptr;
	}
	// Archive-only file, written without truncation: copy up on the first
	// write, not now.
	if (n && n->where == cow::origin::archive && !truncate) {
		if (std::FILE* lazy = open_lazy(l, mode, n->info)) {
			return lazy;
		}
	}
	// Everything else changes the overlay now (create, truncate, or the
	// file is already there).
	const auto host = cow::prepare_write(l, create, false, truncate);
	if (!host) {
		errno = host.error().value();
		return nullptr;
	}
	return std::fopen(host->string().c_str(), mode);
}

} // namespace

std::FILE* fopen(const char* path, const char* mode)
{
	const auto p = resolved(path);
	if (!p) {
		return nullptr;
	}
	if (const auto r = rwz::fs::mounts::find(*p)) {
		if (!r->overlay.empty()) {
			return open_overlay_stream(*r, mode);
		}
		if (has_write_intent(mode)) {
			errno = EROFS;
			return nullptr;
		}
		return open_archive_stream(*r, mode);
	}
	return std::fopen(p->string().c_str(), mode);
}

// STDIO-10
std::size_t fwrite(const void* buf, std::size_t size, std::size_t count,
                   std::FILE* stream)
{
	return std::fwrite(buf, size, count, stream);
}

// STDIO-11
int fseek(std::FILE* stream, long offset, int whence)
{
	return std::fseek(stream, offset, whence);
}

// STDIO-03
long ftell(std::FILE* stream)
{
	return std::ftell(stream);
}

// STDIO-04
int fflush(std::FILE* stream)
{
	return std::fflush(stream);
}

#if !defined(_WIN32)
// STDIO-12
int fseeko(std::FILE* stream, off_t offset, int whence)
{
	return ::fseeko(stream, offset, whence);
}

// STDIO-13
off_t ftello(std::FILE* stream)
{
	return ::ftello(stream);
}
#endif

// STDIO-09
int fclose(std::FILE* stream)
{
#if defined(_WIN32)
	std::filesystem::path copy;
	{
		const std::scoped_lock lock(copy_mutex());
		if (const auto it = copies().find(stream); it != copies().end()) {
			copy = it->second;
			copies().erase(it);
		}
	}
	const int rc = std::fclose(stream);
	if (!copy.empty()) {
		std::error_code rm_ec{};
		std::filesystem::remove(copy, rm_ec);
	}
	return rc;
#else
	return std::fclose(stream); // callback streams release their state in
	                            // their close callback
#endif
}

// STDIO-05
int remove(const char* path)
{
	const auto p = resolved(path, false);
	if (!p) {
		return -1;
	}
	if (const auto r = rwz::fs::mounts::find(*p)) {
		if (!r->overlay.empty()) {
			const auto n   = cow::lookup(cow::layer_of(*r));
			const auto err = !n ? std::make_error_code(
			                              std::errc::no_such_file_or_directory)
			               : n->info.type == rwz::archive::kind::directory
			                       ? cow::remove_dir(cow::layer_of(*r))
			                       : cow::remove_file(cow::layer_of(*r));
			if (err) {
				errno = err.value();
				return -1;
			}
			return 0;
		}
		errno = r->backend ? EROFS : ENOTSUP;
		return -1;
	}
	return std::remove(p->string().c_str());
}

// STDIO-06
int rename(const char* from, const char* to)
{
	const auto src = resolved(from, false);
	if (!src) {
		return -1;
	}
	const auto dst = resolved(to, false);
	if (!dst) {
		return -1;
	}
	const auto rs = rwz::fs::mounts::find(*src);
	const auto rd = rwz::fs::mounts::find(*dst);
	if (rs || rd) {
		if (!rs || !rd || rs->overlay.empty() || rd->overlay.empty()) {
			const auto& r = rs ? *rs : *rd;
			errno         = r.backend ? EROFS : ENOTSUP;
			return -1;
		}
		const auto err = cow::rename_file(cow::layer_of(*rs),
		                                  cow::layer_of(*rd));
		if (err) {
			errno = err.value();
			return -1;
		}
		return 0;
	}
	return std::rename(src->string().c_str(), dst->string().c_str());
}

// STDIO-07. A stream for an archive path is returned in the failed state.
// Not yet supported for archive files: this stream type cannot wrap an
// in-memory copy.
// ---- C++ streams over the stdio wrappers ----------------------------------

namespace {

// The fopen mode for a C++ openmode, as the standard's table gives it (see
// stdio.h), or "" for a combination an input stream rejects.
std::string fopen_mode(std::ios_base::openmode mode, bool input)
{
	const bool in    = (mode & std::ios_base::in) != 0;
	const bool app   = (mode & std::ios_base::app) != 0;
	const bool trunc = (mode & std::ios_base::trunc) != 0;
	std::string m    = {};
	if (input) {
		if (app || trunc) {
			return {};
		}
		m = "r";
	} else if (app) {
		m = in ? "a+" : "a";
	} else if (in) {
		m = trunc ? "w+" : "r+";
	} else {
		m = "w"; // out alone truncates, as in the standard
	}
	if ((mode & std::ios_base::binary) != 0) {
		m += 'b';
	}
	return m;
}

// Opens the buffer for a stream. A failed open leaves the buffer closed; the
// stream sets failbit.
template <typename Buf>
std::shared_ptr<Buf> open_buf(const std::filesystem::path& path,
                              std::ios_base::openmode mode, bool input)
{
	const std::string m = fopen_mode(mode, input);
	std::FILE* f        = nullptr;
	if (m.empty()) {
		errno = EINVAL;
	} else {
		f = rwz::stdio::fopen(path.string().c_str(), m.c_str());
	}
	if (f != nullptr && (mode & std::ios_base::ate) != 0) {
		rwz::stdio::fseek(f, 0, SEEK_END);
	}
	return std::make_shared<Buf>(f);
}

} // namespace

class file_istream::buf final : public std::streambuf {
public:
	explicit buf(std::FILE* f) : f_{f} {}
	~buf() override
	{
		if (f_ != nullptr) {
			rwz::stdio::fclose(f_);
		}
	}
	buf(const buf&)            = delete;
	buf& operator=(const buf&) = delete;
	[[nodiscard]] bool is_open() const noexcept
	{
		return f_ != nullptr;
	}
	int close()
	{
		std::FILE* f = f_;
		f_           = nullptr;
		return f != nullptr ? rwz::stdio::fclose(f) : 0;
	}

protected:
	int_type underflow() override
	{
		if (gptr() < egptr()) {
			return traits_type::to_int_type(*gptr());
		}
		if (f_ == nullptr) {
			return traits_type::eof();
		}
		const std::size_t n = rwz::stdio::fread(storage_.data(),
		                                        1,
		                                        storage_.size(),
		                                        f_);
		if (n == 0) {
			return traits_type::eof();
		}
		setg(storage_.data(), storage_.data(), storage_.data() + n);
		return traits_type::to_int_type(*gptr());
	}

	pos_type seekoff(off_type off, std::ios_base::seekdir dir,
	                 std::ios_base::openmode) override
	{
		if (f_ == nullptr) {
			return pos_type(off_type(-1));
		}
		int whence = SEEK_SET;
		if (dir == std::ios_base::cur) {
			off -= egptr() - gptr(); // the unread buffer is not
			                         // part of the file position
			whence = SEEK_CUR;
		} else if (dir == std::ios_base::end) {
			whence = SEEK_END;
		}
		if (rwz::stdio::fseek(f_, static_cast<long>(off), whence) != 0) {
			return pos_type(off_type(-1));
		}
		setg(nullptr, nullptr, nullptr);
		return pos_type(off_type(rwz::stdio::ftell(f_)));
	}

	pos_type seekpos(pos_type pos, std::ios_base::openmode mode) override
	{
		return seekoff(off_type(pos), std::ios_base::beg, mode);
	}

private:
	std::FILE* f_;
	std::array<char, kStreamBufferBytes> storage_ = {}; // inside the buffer
	                                                    // object: one
	                                                    // allocation per
	                                                    // stream
};

class file_ostream::buf final : public std::streambuf {
public:
	explicit buf(std::FILE* f) : f_{f} {}
	~buf() override
	{
		if (f_ != nullptr) {
			rwz::stdio::fclose(f_);
		}
	}
	buf(const buf&)            = delete;
	buf& operator=(const buf&) = delete;
	[[nodiscard]] bool is_open() const noexcept
	{
		return f_ != nullptr;
	}
	int close()
	{
		std::FILE* f = f_;
		f_           = nullptr;
		return f != nullptr ? rwz::stdio::fclose(f) : 0;
	}

protected:
	int_type overflow(int_type c) override
	{
		if (traits_type::eq_int_type(c, traits_type::eof())) {
			return traits_type::not_eof(c);
		}
		if (f_ == nullptr) {
			return traits_type::eof();
		}
		const char ch = traits_type::to_char_type(c);
		return rwz::stdio::fwrite(&ch, 1, 1, f_) == 1 ? c
		                                              : traits_type::eof();
	}

	std::streamsize xsputn(const char* s, std::streamsize n) override
	{
		if (f_ == nullptr || n <= 0) {
			return 0;
		}
		return static_cast<std::streamsize>(
		        rwz::stdio::fwrite(s, 1, static_cast<std::size_t>(n), f_));
	}

	int sync() override
	{
		return (f_ != nullptr && std::fflush(f_) == 0) ? 0 : -1;
	}

	pos_type seekoff(off_type off, std::ios_base::seekdir dir,
	                 std::ios_base::openmode) override
	{
		if (f_ == nullptr) {
			return pos_type(off_type(-1));
		}
		const int whence = dir == std::ios_base::beg ? SEEK_SET
		                 : dir == std::ios_base::cur ? SEEK_CUR
		                                             : SEEK_END;
		if (rwz::stdio::fseek(f_, static_cast<long>(off), whence) != 0) {
			return pos_type(off_type(-1));
		}
		return pos_type(off_type(rwz::stdio::ftell(f_)));
	}

	pos_type seekpos(pos_type pos, std::ios_base::openmode mode) override
	{
		return seekoff(off_type(pos), std::ios_base::beg, mode);
	}

private:
	std::FILE* f_;
};

// ---- file_istream -----------------------------------------------------------

file_istream::file_istream()
        : std::istream(nullptr),
          buf_(std::make_shared<buf>(nullptr))
{
	rdbuf(buf_.get());
}

file_istream::file_istream(const std::filesystem::path& path,
                           std::ios_base::openmode mode)
        : std::istream(nullptr),
          buf_(open_buf<buf>(path, mode | std::ios_base::in, true))
{
	rdbuf(buf_.get());
	if (!is_open()) {
		setstate(std::ios::failbit);
	}
}

file_istream::file_istream(file_istream&& other) noexcept
        : std::istream(std::move(other)),
          buf_(std::move(other.buf_))
{
	rdbuf(buf_.get()); // the base move does not keep the stream buffer;
	                   // re-attach the one that moved
}

file_istream::~file_istream() = default;

void file_istream::open(const std::filesystem::path& path, std::ios_base::openmode mode)
{
	clear();
	buf_ = open_buf<buf>(path, mode | std::ios_base::in, true);
	rdbuf(buf_.get());
	if (!is_open()) {
		setstate(std::ios::failbit);
	}
}

void file_istream::close()
{
	if (!is_open()) {
		setstate(std::ios::failbit); // as std::ifstream::close does on
		                             // a closed stream
		return;
	}
	if (buf_->close() != 0) {
		setstate(std::ios::failbit);
	}
}

bool file_istream::is_open() const noexcept
{
	return buf_ != nullptr && buf_->is_open();
}

// ---- file_ostream -----------------------------------------------------------

file_ostream::file_ostream()
        : std::ostream(nullptr),
          buf_(std::make_shared<buf>(nullptr))
{
	rdbuf(buf_.get());
}

file_ostream::file_ostream(const std::filesystem::path& path,
                           std::ios_base::openmode mode)
        : std::ostream(nullptr),
          buf_(open_buf<buf>(path, mode | std::ios_base::out, false))
{
	rdbuf(buf_.get());
	if (!is_open()) {
		setstate(std::ios::failbit);
	}
}

file_ostream::file_ostream(file_ostream&& other) noexcept
        : std::ostream(std::move(other)),
          buf_(std::move(other.buf_))
{
	rdbuf(buf_.get());
}

file_ostream::~file_ostream() = default;

void file_ostream::open(const std::filesystem::path& path, std::ios_base::openmode mode)
{
	clear();
	buf_ = open_buf<buf>(path, mode | std::ios_base::out, false);
	rdbuf(buf_.get());
	if (!is_open()) {
		setstate(std::ios::failbit);
	}
}

void file_ostream::close()
{
	if (!is_open()) {
		setstate(std::ios::failbit);
		return;
	}
	if (buf_->close() != 0) {
		setstate(std::ios::failbit);
	}
}

bool file_ostream::is_open() const noexcept
{
	return buf_ != nullptr && buf_->is_open();
}

file_istream input_file_stream(const std::string& path)
{
	return file_istream(path);
}

file_ostream output_file_stream(const std::string& path)
{
	return file_ostream(path);
}

} // namespace rwz::stdio
