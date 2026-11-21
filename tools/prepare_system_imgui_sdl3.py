#!/usr/bin/env python3
"""Adapt distro-provided Dear ImGui SDL3 backends to released SDL3 APIs.

Debian 13's ImGui 1.90.1 package ships 2023 pre-release SDL3 backend sources,
while its libsdl3-dev uses the 2024+ stable API. Compile private adapted
copies: never edit installed /usr/share/doc/libimgui-dev files.

This is intentionally narrow. ImGui's *core* still comes from the system.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re


_RENAMES = {
    "SDLK_QUOTE": "SDLK_APOSTROPHE",
    "SDLK_BACKQUOTE": "SDLK_GRAVE",
    "SDL_SYSTEM_CURSOR_ARROW": "SDL_SYSTEM_CURSOR_DEFAULT",
    "SDL_SYSTEM_CURSOR_IBEAM": "SDL_SYSTEM_CURSOR_TEXT",
    "SDL_SYSTEM_CURSOR_SIZEALL": "SDL_SYSTEM_CURSOR_MOVE",
    "SDL_SYSTEM_CURSOR_SIZENS": "SDL_SYSTEM_CURSOR_NS_RESIZE",
    "SDL_SYSTEM_CURSOR_SIZEWE": "SDL_SYSTEM_CURSOR_EW_RESIZE",
    "SDL_SYSTEM_CURSOR_SIZENESW": "SDL_SYSTEM_CURSOR_NESW_RESIZE",
    "SDL_SYSTEM_CURSOR_SIZENWSE": "SDL_SYSTEM_CURSOR_NWSE_RESIZE",
    "SDL_SYSTEM_CURSOR_HAND": "SDL_SYSTEM_CURSOR_POINTER",
    "SDL_SYSTEM_CURSOR_NO": "SDL_SYSTEM_CURSOR_NOT_ALLOWED",
    "SDL_TRUE": "true",
    "SDL_FALSE": "false",
}

# Convert the SDL_Color vertex data expected by early ImGui SDL3 releases
# into SDL_FColor, required by the SDL3 3.2+ SDL_RenderGeometryRaw API.
# SDL_Color is packed RGBA8; SDL_FColor contains four floats (0.0 .. 1.0).
_RENDER_GEOMETRY_COMPAT = r"""
#include <SDL3/SDL.h>
#include <vector>

static bool KotonohaSDLRenderGeometryRaw(
    SDL_Renderer* renderer, SDL_Texture* texture,
    const float* xy, int xy_stride, const SDL_Color* color, int color_stride,
    const float* uv, int uv_stride, int num_vertices,
    const void* indices, int num_indices, int size_indices)
{
    std::vector<SDL_FColor> floats(static_cast<size_t>(num_vertices));
    for (int i = 0; i < num_vertices; ++i)
    {
        const auto* packed = reinterpret_cast<const SDL_Color*>(
            reinterpret_cast<const unsigned char*>(color) + i * color_stride);
        floats[static_cast<size_t>(i)] = {
            packed->r / 255.0f, packed->g / 255.0f,
            packed->b / 255.0f, packed->a / 255.0f};
    }
    return SDL_RenderGeometryRaw(
        renderer, texture, xy, xy_stride, floats.data(),
        static_cast<int>(sizeof(SDL_FColor)), uv, uv_stride,
        num_vertices, indices, num_indices, size_indices);
}
#define SDL_RenderGeometryRaw KotonohaSDLRenderGeometryRaw

"""


def adapt_platform(source: str) -> str:
    # Recent SDL3 moved the keyboard sym/scancode directly onto SDL_KeyboardEvent.
    source = source.replace("event->key.keysym.scancode", "event->key.scancode")
    source = source.replace("event->key.keysym.sym", "event->key.key")
    # IME text-input area now requires the originating window and cursor.
    source = source.replace(
        "SDL_SetTextInputRect(&r);",
        "SDL_SetTextInputArea(ImGui_ImplSDL3_GetBackendData()->Window, &r, 0);",
    )
    # SDL3 keycode literals and system cursor names were standardized.
    source = re.sub(
        r"\bSDLK_([a-z])\b",
        lambda match: "SDLK_" + match.group(1).upper(),
        source,
    )
    for original, replacement in _RENAMES.items():
        source = re.sub(r"\b" + original + r"\b", replacement, source)
    return source


def adapt_renderer(source: str) -> str:
    source = re.sub(r"\bSDL_TRUE\b", "true", source)
    source = re.sub(r"\bSDL_FALSE\b", "false", source)
    if ("const SDL_Color* color" in source
            and "SDL_RenderGeometryRaw(" in source
            and "KotonohaSDLRenderGeometryRaw" not in source):
        source = _RENDER_GEOMETRY_COMPAT + source
    return source


def write_if_changed(path: Path, source: str) -> None:
    if not path.exists() or path.read_text(encoding="utf-8") != source:
        path.write_text(source, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", type=Path, required=True)
    parser.add_argument("--renderer", type=Path, required=True)
    parser.add_argument("--outdir", type=Path, required=True)
    args = parser.parse_args()
    if not args.platform.is_file() or not args.renderer.is_file():
        parser.error("Expected both ImGui system SDL3 backend .cpp source files")
    args.outdir.mkdir(parents=True, exist_ok=True)
    platform = adapt_platform(args.platform.read_text(encoding="utf-8"))
    renderer = adapt_renderer(args.renderer.read_text(encoding="utf-8"))
    write_if_changed(args.outdir / "imgui_impl_sdl3.cpp", platform)
    write_if_changed(args.outdir / "imgui_impl_sdlrenderer3.cpp", renderer)
    print("[Kotonoha] Prepared distro ImGui SDL3 backend sources for installed SDL3")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
