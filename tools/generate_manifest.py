#!/usr/bin/env python3
"""Build a size-aware manifest.json for any existing assets directory.

Usage:
    python3 tools/generate_manifest.py /path/to/SteinsGate
    python3 tools/generate_manifest.py build-web/outputs/N2SystemGame

If the supplied directory has a game-assets/ subdirectory, index its contents.
Otherwise, index the supplied directory itself. In either case manifest.json is
written in the supplied directory; entry paths are relative to the asset root.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path


EXCLUDED_FILES = {".gitignore", "README.txt", "manifest.json",
                  "manifest.json.tmp", "preload.json"}
EXCLUDED_DIRECTORIES = {".git", ".svn", "__pycache__"}


def generate_manifest(directory: Path) -> Path:
    root = directory.expanduser().resolve()
    if not root.is_dir():
        raise ValueError(f"Directory does not exist: {root}")

    # A packaged WASM page has game-assets/ next to index.html. Standalone
    # retail game folders have their archives directly in the supplied root.
    assets = root / "game-assets" if (root / "game-assets").is_dir() else root
    entries = []
    for folder, subfolders, filenames in os.walk(assets):
        subfolders[:] = sorted(
            name for name in subfolders
            if name not in EXCLUDED_DIRECTORIES and
            not (Path(folder) / name).is_symlink()
        )
        for name in sorted(filenames):
            file = Path(folder) / name
            if name in EXCLUDED_FILES or file.is_symlink() or not file.is_file():
                continue
            entries.append({
                "path": file.relative_to(assets).as_posix(),
                "size": file.stat().st_size,
            })
    entries.sort(key=lambda item: item["path"])
    manifest = {"files": [item["path"] for item in entries], "entries": entries}

    output = root / "manifest.json"
    temporary = root / "manifest.json.tmp"
    with temporary.open("w", encoding="utf-8") as handle:
        json.dump(manifest, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    os.replace(temporary, output)
    print(f"[Kotonoha] Generated {output} with {len(entries)} assets "
          f"(scanned {assets})")
    if not entries:
        print("[Kotonoha] WARNING: No assets found; HTTP(S) startup requires "
              "a non-empty manifest.json.")
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory",
                        help="Any assets folder or a web page root with game-assets/")
    args = parser.parse_args()
    try:
        generate_manifest(Path(args.directory))
    except (OSError, ValueError) as error:
        parser.error(str(error))
