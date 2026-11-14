#!/usr/bin/env python3
"""Validate side-module files deployed with every browser entry point."""
from pathlib import Path
import sys

def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: check_wasm_dylink.py <outputs-dir>")
    root = Path(sys.argv[1])
    games = sorted(p for p in root.iterdir()
                   if p.is_dir() and any(p.glob("*.js")))
    if not games:
        raise SystemExit("No browser outputs found")
    for game in games:
        for name in ("libkotonoha.wasm", "libkotonoha_imgui.wasm"):
            file = game / name
            if not file.is_file() or file.stat().st_size < 8:
                raise SystemExit(f"{game.name}: missing side module {name}")
            with file.open("rb") as stream:
                if stream.read(4) != b"\x00asm":
                    raise SystemExit(f"{game.name}: invalid WASM: {name}")
        print(f"[Kotonoha] {game.name}: both side modules found")
    print(f"[Kotonoha] inspected {len(games)} output directories")

if __name__ == "__main__":
    main()
