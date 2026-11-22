#!/usr/bin/env python3
"""Generate <page>/manifest.json from <page>/game-assets.

Usage: python3 tools/generate_manifest.py build-web/outputs/N2SystemGame
Game paths in the index are relative to game-assets/, not to the page root.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path


def generate_manifest(page_directory: Path) -> Path:
    page = page_directory.expanduser().resolve()
    if not page.is_dir():
        raise ValueError(f"Web page directory not found: {page}")
    assets = page / "game-assets"
    entries = []
    if assets.is_dir():
        for folder, subfolders, filenames in os.walk(assets):
            subfolders[:] = sorted(name for name in subfolders
                                   if not (Path(folder) / name).is_symlink())
            for name in sorted(filenames):
                file = Path(folder) / name
                if name in {".gitignore", "README.txt", "manifest.json", "preload.json"}:
                    continue
                if file.is_symlink() or not file.is_file():
                    continue
                entries.append({"path": file.relative_to(assets).as_posix(),
                                "size": file.stat().st_size})
    entries.sort(key=lambda item: item["path"])
    manifest = {"files": [item["path"] for item in entries], "entries": entries}
    output = page / "manifest.json"
    temporary = page / "manifest.json.tmp"
    with temporary.open("w", encoding="utf-8") as file:
        json.dump(manifest, file, ensure_ascii=False, indent=2)
        file.write("\n")
    os.replace(temporary, output)
    print(f"[Kotonoha] Generated {output} with {len(entries)} assets")
    if not entries:
        print("[Kotonoha] WARNING: HTTP(S) mode requires non-empty game-assets/; "
              "local-folder mode does not use a manifest.")
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("page_directory", help="Directory with index.html and game-assets/")
    args = parser.parse_args()
    try:
        generate_manifest(Path(args.page_directory))
    except (OSError, ValueError) as error:
        parser.error(str(error))
