#!/usr/bin/env python3
"""Validate per-game portable CPack ZIPs without extracting game data."""
from __future__ import annotations

import sys
import zipfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: check_game_zip.py <packages-directory> <game-target>")
        return 2
    root = Path(sys.argv[1])
    target = sys.argv[2]
    archives = sorted(root.rglob("*.zip"), key=lambda p: p.stat().st_mtime)
    archives = [p for p in archives if p.name.endswith(f"-{target}.zip")]
    if not archives:
        print(f"[Kotonoha] No CPack ZIP for {target} in {root}")
        return 1
    archive = archives[-1]
    with zipfile.ZipFile(archive) as packed:
        names = packed.namelist()
        if not any(p.endswith(f"/{target}/{target}") for p in names):
            # CPack may not add an outer prefix depending on its generator.
            if f"{target}/{target}" not in names:
                print(f"[Kotonoha] ZIP missing {target} executable: {archive}")
                return 1
        libs = [p for p in names if "/libs/" in p and not p.endswith("/")]
        if sys.platform != "win32" and not any("kotonoha" in p.lower()
                                                  for p in libs):
            print(f"[Kotonoha] ZIP missing engine shared library: {archive}")
            return 1
        if not any(f"/{target}/assets/" in "/" + p for p in names):
            print(f"[Kotonoha] ZIP missing engine assets: {archive}")
            return 1
        if any("/game-assets/" in "/" + p for p in names) and target != "Kotonoha":
            print(f"[Kotonoha] Unexpected commercial game assets: {archive}")
            return 1
        print(f"[Kotonoha] ZIP verified: {archive} ({len(names)} members, "
              f"{len(libs)} runtime library entries)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
