/*
 * C-callable bridge from SDL's file opening to rwzfs. Include this from C code,
 * and link rwz_sdl_io.cpp into the same target. Opening by path goes through
 * rwz::stdio, so a path under the mounted archive root reads from the archive
 * (and writes go to the overlay), exactly as it does for the library's own
 * calls. Use these in place of SDL_IOFromFile (SDL3) or SDL_RWFromFile (SDL2).
 * The vendoring tool rewrites the calls to these names.
 *
 * Only the declaration of the matching SDL version is defined; the other is
 * left undefined.
 */
#ifndef RWZ_SDL_IO_H
#define RWZ_SDL_IO_H

/* Forward declarations only: the struct tags match SDL's own, so no SDL header
 * is needed here. */
struct SDL_IOStream;
struct SDL_RWops;

#ifdef __cplusplus
extern "C" {
#endif

/* SDL3: same contract as SDL_IOFromFile(file, mode). Returns NULL and sets the
 * SDL error on failure. */
struct SDL_IOStream* rwz_SDL_IOFromFile(const char* file, const char* mode);

/* SDL2: same contract as SDL_RWFromFile(file, mode). Returns NULL and sets the
 * SDL error on failure. */
struct SDL_RWops* rwz_SDL_RWFromFile(const char* file, const char* mode);

#ifdef __cplusplus
}
#endif

#endif /* RWZ_SDL_IO_H */
