#!/usr/bin/env python3
"""Validate the game WASM and both Emscripten SIDE_MODULE binaries."""
from pathlib import Path
import sys


def read_uleb(stream):
    result = 0
    for shift in range(0, 35, 7):
        byte = stream.read(1)
        if not byte:
            raise ValueError("truncated unsigned LEB128")
        result |= (byte[0] & 0x7F) << shift
        if not byte[0] & 0x80:
            return result
    raise ValueError("invalid unsigned LEB128")


def is_dynamic_module(path):
    with path.open("rb") as stream:
        if stream.read(8) != b"\x00asm\x01\x00\x00\x00":
            return False
        while stream.tell() < path.stat().st_size:
            marker = stream.read(1)
            if not marker:
                break
            section_size = read_uleb(stream)
            end = stream.tell() + section_size
            if end > path.stat().st_size:
                return False
            if marker == b"\x00":
                name_length = read_uleb(stream)
                name = stream.read(name_length)
                if name in (b"dylink", b"dylink.0"):
                    return True
            stream.seek(end)
    return False


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: check_wasm_dylink.py <outputs-dir>")
    root = Path(sys.argv[1])
    games = sorted(p for p in root.iterdir()
                   if p.is_dir() and any(p.glob("*.js")))
    if not games:
        raise SystemExit("No browser application outputs found")
    for game in games:
        for name in ("libkotonoha.wasm", "libkotonoha_imgui.wasm"):
            file = game / name
            if not file.is_file() or not is_dynamic_module(file):
                raise SystemExit(f"{game.name}: missing/invalid Emscripten SIDE_MODULE: {name}")
        mains = [x for x in game.glob("*.wasm")
                 if x.name not in ("libkotonoha.wasm", "libkotonoha_imgui.wasm")]
        if not mains:
            raise SystemExit(f"{game.name}: missing game main WASM")
        print(f"[Kotonoha] {game.name}: game and both dynamic side modules present")
    print(f"[Kotonoha] validated {len(games)} browser game outputs")


if __name__ == "__main__":
    main()
