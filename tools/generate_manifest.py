#!/usr/bin/env python3
"""Generate a size-aware HTTP asset manifest: [pasta] [arquivo].

Usage:
    python3 tools/generate_manifest.py /path/to/game-assets /path/to/manifest.json
    python3 tools/generate_manifest.py /path/to/SteinsGate /path/to/site/manifest.json

[pasta] is the directory to index recursively, without implicit subfolder
selection. [arquivo] is the exact output file path (absolute or relative to
the current working directory). Asset paths in the JSON are relative to [pasta].
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path


EXCLUDED_FILES = {".gitignore", "README.txt", "manifest.json",
                  "manifest.json.tmp", "preload.json"}
EXCLUDED_DIRECTORIES = {".git", ".svn", "__pycache__"}


def generate_manifest(directory: Path, filename: Path) -> Path:
    assets = directory.expanduser().resolve()
    if not assets.is_dir():
        raise ValueError(f"Directory does not exist: {assets}")

    output = filename.expanduser().resolve()
    if output.is_dir():
        raise ValueError(f"Manifest output must be a file: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")

    entries = []
    for folder, subfolders, filenames in os.walk(assets):
        subfolders[:] = sorted(
            name for name in subfolders
            if name not in EXCLUDED_DIRECTORIES and
            not (Path(folder) / name).is_symlink()
        )
        for name in sorted(filenames):
            file = Path(folder) / name
            if (name in EXCLUDED_FILES or file == output or file == temporary
                    or file.is_symlink() or not file.is_file()):
                continue
            entries.append({
                "path": file.relative_to(assets).as_posix(),
                "size": file.stat().st_size,
            })
    entries.sort(key=lambda item: item["path"])
    manifest = {"files": [item["path"] for item in entries], "entries": entries}

    try:
        with temporary.open("w", encoding="utf-8") as handle:
            json.dump(manifest, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        os.replace(temporary, output)
    finally:
        if temporary.exists():
            temporary.unlink()

    print(f"[Kotonoha] Generated {output} with {len(entries)} assets "
          f"(scanned {assets})")
    if not entries:
        print("[Kotonoha] WARNING: No assets found; HTTP(S) startup requires "
              "a non-empty manifest.json.")
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", metavar="pasta",
                        help="Existing directory to index recursively")
    parser.add_argument("output", metavar="arquivo",
                        help="Exact output manifest filename")
    args = parser.parse_args()
    try:
        generate_manifest(Path(args.directory), Path(args.output))
    except (OSError, ValueError) as error:
        parser.error(str(error))
