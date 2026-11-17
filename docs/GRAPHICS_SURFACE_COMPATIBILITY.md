# Shared surface compatibility and transparency

[Português (Brasil)](GRAPHICS_SURFACE_COMPATIBILITY.pt-BR.md)

## Problem

Visual-novel artwork may use alpha channels, RGB color keys, or indexed
palettes with a transparent index. Uploading a decoded surface directly to an
SDL texture without resolving the color key can leave opaque backgrounds.
Interpolating transparent texels with black RGB can introduce dark edges.

## Shared implementation

`prepareSurfaceForLinearSampling(SDL_Surface *)` creates a new RGBA32
surface for the engine's texture-upload paths. It preserves the input surface
and its blend mode, palette and color key. For keyed images, the original
pixel-format-aware SDL blitter copies visible pixels onto transparent RGBA32;
this also respects indexed keys even when two palette entries share the same
RGB color. Other image formats are converted with `SDL_ConvertSurface`.

The existing one-pixel straight-alpha edge preparation is applied after
normalization, without changing any output alpha or visible pixel. The
prepared surface is used by `AssetProvider::stageTexture`, provider image
scaling (including upscaler inputs), and shared splash-screen uploads.

## Tests and limitations

- `Kotonoha.MediaAnalysis`: synthetic RGB24, BGRA32, indexed palette
  keys, preservation of the source surface, bilinear scaled overlays and
  composition on an opaque 800×600 framebuffer.
- `TextureAlphaStandalone.cpp`: C++17, no SDL or game assets; tests RGBA
  edge padding, non-tight pitches and invalid geometry. The
  `texture-alpha.yml` workflow runs it on PRs and `dev` pushes.
- Actual renderer- or GPU-specific output still needs integration testing
  with each platform and upscaler.
- This change does not reverse-engineer unknown proprietary image formats
  or implement game-specific menus, gallery screens or native script calls.
  It does not bundle original artwork.
