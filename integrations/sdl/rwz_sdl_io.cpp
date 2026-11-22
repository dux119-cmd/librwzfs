// Implementation of rwz_sdl_io.h. Compiled as C++ into the target that uses
// SDL's file opening. The SDL version is chosen by the headers the target
// already has. Written against the public SDL3 and SDL2 APIs: check it against
// the SDL version your project builds with.
#include <cstdint>
#include <cstdio>

#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include "rwz_sdl_io.h"
#include "rwzfs/rwz.h"

namespace {

struct rwz_file {
	std::FILE* fp;
	std::uint64_t size; // the size at open: SDL_sound uses it for the duration
};

// Length by seeking to the end; the position is restored.
std::uint64_t measure(std::FILE* fp)
{
	rwz::stdio::fseek(fp, 0, SEEK_END);
	const long end = rwz::stdio::ftell(fp);
	rwz::stdio::fseek(fp, 0, SEEK_SET);
	return end < 0 ? 0 : static_cast<std::uint64_t>(end);
}

std::FILE* open_file(const char* file, const char* mode, rwz_file& out)
{
	std::FILE* fp = rwz::stdio::fopen(file, mode);
	if (fp != nullptr) {
		out = rwz_file{fp, measure(fp)};
	}
	return fp;
}

} // namespace

#if SDL_MAJOR_VERSION >= 3

namespace {

Sint64 sdl3_size(void* userdata)
{
	return static_cast<Sint64>(static_cast<rwz_file*>(userdata)->size);
}

Sint64 sdl3_seek(void* userdata, Sint64 offset, SDL_IOWhence whence)
{
	auto* f     = static_cast<rwz_file*>(userdata);
	const int w = whence == SDL_IO_SEEK_CUR ? SEEK_CUR
	            : whence == SDL_IO_SEEK_END ? SEEK_END
	                                        : SEEK_SET;
	if (rwz::stdio::fseek(f->fp, static_cast<long>(offset), w) != 0) {
		return -1;
	}
	return static_cast<Sint64>(rwz::stdio::ftell(f->fp));
}

size_t sdl3_read(void* userdata, void* ptr, size_t size, SDL_IOStatus* status)
{
	auto* f          = static_cast<rwz_file*>(userdata);
	const size_t got = rwz::stdio::fread(ptr, 1, size, f->fp);
	if (got < size) {
		const long pos = rwz::stdio::ftell(f->fp);
		*status = (pos >= 0 && static_cast<std::uint64_t>(pos) >= f->size)
		                ? SDL_IO_STATUS_EOF
		                : SDL_IO_STATUS_ERROR;
	}
	return got;
}

size_t sdl3_write(void* userdata, const void* ptr, size_t size, SDL_IOStatus* status)
{
	auto* f          = static_cast<rwz_file*>(userdata);
	const size_t put = rwz::stdio::fwrite(ptr, 1, size, f->fp);
	if (put < size) {
		*status = SDL_IO_STATUS_ERROR;
	}
	return put;
}

bool sdl3_flush(void* userdata, SDL_IOStatus*)
{
	return std::fflush(static_cast<rwz_file*>(userdata)->fp) == 0;
}

bool sdl3_close(void* userdata)
{
	auto* f       = static_cast<rwz_file*>(userdata);
	const bool ok = rwz::stdio::fclose(f->fp) == 0;
	delete f;
	return ok;
}

} // namespace

extern "C" SDL_IOStream* rwz_SDL_IOFromFile(const char* file, const char* mode)
{
	auto* f = new rwz_file{};
	if (open_file(file, mode, *f) == nullptr) {
		delete f;
		SDL_SetError("rwz: cannot open %s", file);
		return nullptr;
	}
	SDL_IOStreamInterface iface;
	SDL_INIT_INTERFACE(&iface);
	iface.size       = sdl3_size;
	iface.seek       = sdl3_seek;
	iface.read       = sdl3_read;
	iface.write      = sdl3_write;
	iface.flush      = sdl3_flush;
	iface.close      = sdl3_close;
	SDL_IOStream* io = SDL_OpenIO(&iface, f);
	if (io == nullptr) {
		sdl3_close(f); // SDL_OpenIO failed: release the file
	}
	return io;
}

#else // SDL2

namespace {

Sint64 sdl2_size(SDL_RWops* ops)
{
	return static_cast<Sint64>(
	        static_cast<rwz_file*>(ops->hidden.unknown.data1)->size);
}

Sint64 sdl2_seek(SDL_RWops* ops, Sint64 offset, int whence)
{
	auto* f     = static_cast<rwz_file*>(ops->hidden.unknown.data1);
	const int w = whence == RW_SEEK_CUR ? SEEK_CUR
	            : whence == RW_SEEK_END ? SEEK_END
	                                    : SEEK_SET;
	if (rwz::stdio::fseek(f->fp, static_cast<long>(offset), w) != 0) {
		return -1;
	}
	return static_cast<Sint64>(rwz::stdio::ftell(f->fp));
}

size_t sdl2_read(SDL_RWops* ops, void* ptr, size_t size, size_t maxnum)
{
	auto* f = static_cast<rwz_file*>(ops->hidden.unknown.data1);
	return rwz::stdio::fread(ptr, size, maxnum, f->fp);
}

size_t sdl2_write(SDL_RWops* ops, const void* ptr, size_t size, size_t num)
{
	auto* f = static_cast<rwz_file*>(ops->hidden.unknown.data1);
	return rwz::stdio::fwrite(ptr, size, num, f->fp);
}

int sdl2_close(SDL_RWops* ops)
{
	auto* f      = static_cast<rwz_file*>(ops->hidden.unknown.data1);
	const int rc = rwz::stdio::fclose(f->fp) == 0 ? 0 : -1;
	delete f;
	SDL_FreeRW(ops);
	return rc;
}

} // namespace

extern "C" SDL_RWops* rwz_SDL_RWFromFile(const char* file, const char* mode)
{
	auto* f = new rwz_file{};
	if (open_file(file, mode, *f) == nullptr) {
		delete f;
		SDL_SetError("rwz: cannot open %s", file);
		return nullptr;
	}
	SDL_RWops* ops = SDL_AllocRW();
	if (ops == nullptr) {
		rwz::stdio::fclose(f->fp);
		delete f;
		return nullptr;
	}
	ops->size                 = sdl2_size;
	ops->seek                 = sdl2_seek;
	ops->read                 = sdl2_read;
	ops->write                = sdl2_write;
	ops->close                = sdl2_close;
	ops->type                 = SDL_RWOPS_UNKNOWN;
	ops->hidden.unknown.data1 = f;
	return ops;
}

#endif
