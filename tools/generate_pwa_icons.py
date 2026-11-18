#!/usr/bin/env python3
"""Generate distinct, maskable Kotonoha PWA icons per WASM game.

Only Python's standard library is needed. The K mark stays inside the central
safe area, while each app gets a stable title-derived accent color.
"""
from __future__ import annotations

import hashlib
import math
import struct
import sys
import zlib
from pathlib import Path

PALETTE = (
    (37, 99, 235), (14, 165, 163), (124, 58, 237),
    (220, 69, 104), (21, 128, 129), (202, 111, 33),
    (68, 105, 174), (130, 75, 135),
)
PNG_SIG = b"\x89PNG\r\n\x1a\n"


def segment_distance_sq(x, y, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    projection = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) /
                               (dx * dx + dy * dy)))
    return (x - ax - projection * dx) ** 2 + \
           (y - ay - projection * dy) ** 2


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + \
        struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def draw_icon(size: int, color: tuple[int, int, int]) -> bytes:
    rows = bytearray()
    for y in range(size):
        rows.append(0)  # PNG filter: None
        fy = (y + 0.5) / size
        for x in range(size):
            fx = (x + 0.5) / size
            # Deep gradient background with game-specific accent.
            shade = 0.09 + 0.12 * (1.0 - fy) + 0.08 * fx
            rgb = [int(9 + c * shade) for c in color]

            # Subtle identity-colored disc; remains outside the logo stroke.
            r2 = (fx - 0.5) ** 2 + (fy - 0.5) ** 2
            if r2 < 0.33 ** 2:
                glow = max(0.0, 1.0 - r2 / (0.33 ** 2))
                rgb = [min(255, int(v + c * 0.46 * glow))
                       for v, c in zip(rgb, color)]

            # Rounded, white Kotonoha K monogram at safe inset.
            strokes = (
                (0.375, 0.31, 0.375, 0.69),
                (0.615, 0.31, 0.385, 0.51),
                (0.395, 0.50, 0.635, 0.69),
            )
            distance_sq = min(segment_distance_sq(fx, fy, *s)
                              for s in strokes)
            radius = 0.041
            antialias = 1.4 / size
            if distance_sq < (radius + antialias) ** 2:
                distance = math.sqrt(distance_sq)
                coverage = max(0.0, min(1.0, (radius + antialias -
                                               distance) / (2 * antialias)))
                rgb = [int(v * (1 - coverage) + 248 * coverage) for v in rgb]
            rows.extend(rgb)

    header = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    return (PNG_SIG + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(bytes(rows), 9)) +
            chunk(b"IEND", b""))


def main() -> int:
    if len(sys.argv) != 3 or not sys.argv[1].strip():
        print("Usage: generate_pwa_icons.py <target> <output-directory>",
              file=sys.stderr)
        return 2
    target, directory = sys.argv[1], Path(sys.argv[2])
    directory.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256(target.encode("utf-8")).digest()
    color = PALETTE[digest[0] % len(PALETTE)]
    for size in (180, 192, 512):
        (directory / f"pwa-icon-{size}.png").write_bytes(
            draw_icon(size, color))
    print(f"[Kotonoha PWA] Created icons for {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
