#pragma once

struct SDL_Surface;

namespace Kotonoha {

// Export a surface as PNG on SDL3 3.2 and newer without SDL3_image.
// SDL3 >= 3.4 provides its own PNG writer; earlier versions use a small
// dependency-free PNG fallback for tools and test screenshots.
bool SavePng(SDL_Surface *surface, const char *filename);

}  // namespace Kotonoha
