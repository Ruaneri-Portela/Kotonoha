#pragma once

struct SDL_Surface;

namespace Kotonoha {

// Output container/codec selected by the caller instead of by a filename
// suffix. Both formats are encoded through FFmpeg (libavcodec +
// libavformat + libswscale); there is no SDL image writer or software
// fallback in this path.
enum class ImageFormat { Png, Jpeg };

// Encodes `surface` into `path` using `format`. Returns false and logs the
// FFmpeg/SDL error through SDL_Log on failure.
bool SaveImage(SDL_Surface *surface, const char *path, ImageFormat format);

// Convenience wrappers for tools/tests that still speak in file formats.
bool SavePng(SDL_Surface *surface, const char *path);
bool SaveJpeg(SDL_Surface *surface, const char *path);

} // namespace Kotonoha
