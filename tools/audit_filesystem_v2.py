#!/usr/bin/env python3
"""Audit direct runtime file I/O and Filesystem V2 build wiring.

This is a source-only migration report, NOT a C++ build or browser test.
Usage:
    python3 tools/audit_filesystem_v2.py
    python3 tools/audit_filesystem_v2.py --strict

No third-party dependencies. The --strict mode fails when the runtime still
contains direct legacy file readers/writers. Metadata checks always fail if
the V2 source list is not wired into Xcode.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BAD_IO = re.compile(
    r"\bstd::(?:i|o)?fstream\b"
    r"|\b(?:std::)?f(?:open|read|write|seek|tell|close)\s*\("
    r"|\b(?:std::)?FILE\s*\*"
)
GAME = re.compile(r'"([A-Z_]+)\|([^|"\n]+)\|(games/[^"\n]+)"')
OLD_HEADER = re.compile(r'#\s*include\s*[<"]Kotonoha/core/FileSystem\.hpp[>"]')


def runtime_files():
    for root in (ROOT / "lib/src", ROOT / "modules", ROOT / "games"):
        if not root.exists():
            continue
        for path in sorted(root.rglob("*")):
            if path.suffix not in {".cpp", ".cc", ".c", ".mm", ".hpp", ".h"}:
                continue
            parts = path.relative_to(ROOT).parts
            if any(p in {"tests", "tools", "vendor", "submodules", "third_party"}
                   for p in parts):
                continue
            if root.name == "modules" and "src" not in parts and "include" not in parts:
                continue
            if root.name == "games" and "src" not in parts and "include" not in parts:
                continue
            yield path


def file_issues(path: Path) -> list[tuple[int, str]]:
    content = path.read_text(encoding="utf-8", errors="replace")
    return [
        (line_number, line.strip())
        for line_number, line in enumerate(content.splitlines(), 1)
        if (BAD_IO.search(line) or OLD_HEADER.search(line))
        # Writing diagnostics to stdout is console I/O, not file access.
        and not ("std::fwrite(" in line and "stdout" in line)
    ]


def validate_build_metadata() -> list[str]:
    issues = []
    source_dir = ROOT / "lib/src/Filesystem"
    sources = sorted(source_dir.rglob("*.cpp"))
    xcode = ROOT / "buildsystems/xcode/Kotonoha.xcodeproj/project.pbxproj"
    cmake = ROOT / "lib/CMakeLists.txt"
    if not sources:
        return ["No Filesystem V2 C++ source files found"]
    if not xcode.is_file() or not cmake.is_file():
        return ["Missing Xcode project or lib/CMakeLists.txt"]

    project = xcode.read_text(encoding="utf-8")
    build = cmake.read_text(encoding="utf-8")
    if "GLOB_RECURSE KOTONOHA_ENGINE_SOURCES" not in build:
        issues.append("CMake engine sources are no longer recursively enumerated")
    for src in sources:
        source = src.relative_to(ROOT).as_posix()
        if project.count("../../" + source) != 1:
            issues.append(f"{source}: missing or duplicated Xcode file reference")
        # The Xcode project has distinct macOS/iOS PBXBuildFile IDs.
        name = src.name + " in Sources"
        if project.count("/* " + name + " */") < 4:
            issues.append(f"{source}: missing a macOS/iOS source phase")
    manifest = ROOT / "tools/generate_manifest.py"
    if not manifest.is_file():
        issues.append("Missing tools/generate_manifest.py")
    else:
        data = manifest.read_text(encoding="utf-8")
        if '"entries"' not in data or '"files"' not in data:
            issues.append("Remote manifest must retain legacy files and sized entries")
    return issues


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true",
                        help="Fail when any runtime direct standard/stdio file I/O remains")
    options = parser.parse_args()
    rows = []
    by_area = {}
    for path in runtime_files():
        relative = path.relative_to(ROOT).as_posix()
        issues = file_issues(path)
        if issues:
            rows.append((relative, issues))
            category = relative.split("/")[0]
            by_area[category] = by_area.get(category, 0) + len(issues)
    games_file = ROOT / "buildsystems/cmake/KotonohaGames.cmake"
    game_matrix = (
        GAME.findall(games_file.read_text(encoding="utf-8"))
        if games_file.exists() else []
    )
    wiring = validate_build_metadata()
    print(f"[Kotonoha] Filesystem V2 audit: {len(game_matrix)} registered games")
    print(f"[Kotonoha] Runtime direct I/O occurrences: "
          f"{sum(len(entries) for _, entries in rows)} in {len(rows)} files")
    for path, hits in rows:
        for line, text in hits:
            print(f"  {path}:{line}: {text}")
    for key, value in sorted(by_area.items()):
        print(f"[Kotonoha] {key}: {value} direct I/O occurrences")
    for warning in wiring:
        print(f"[Kotonoha] BUILD METADATA ERROR: {warning}", file=sys.stderr)
    if wiring:
        return 2
    if rows and options.strict:
        return 1
    print("[Kotonoha] Metadata OK. This is not compilation, CTest, "
          "platform validation, or a confirmation that every game is virtual.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
