#!/usr/bin/env python3
"""Reject direct Emscripten platform logic inside game implementations.

The engine (lib/) and its buildsystem own platform-specific behavior.
Game C++ and game CMakeLists must be cross-platform.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
GAMES = ROOT / "games"
SOURCE_SUFFIXES = {".cpp", ".cc", ".c", ".hpp", ".h", ".mm"}
PATTERNS = [
    re.compile(r"\b(?:__EMSCRIPTEN__|EMSCRIPTEN|emscripten_[A-Za-z_0-9]+)\b", re.I),
    re.compile(r"#\s*include\s*[<\"]emscripten/", re.I),
    re.compile(r"\bEM_ASM(?:_[A-Z]+)?\b"),
    re.compile(r"--preload-file|\-s(?:ASYNCIFY|MAIN_MODULE|SIDE_MODULE|FETCH)\b"),
]


def check():
    failures = []
    for path in GAMES.rglob("*"):
        if not path.is_file() or not (
            path.suffix.lower() in SOURCE_SUFFIXES or path.name == "CMakeLists.txt"
        ):
            continue
        # Generated and vendored sources are external to game ports.
        if any(part in {"build", "vendor", "third_party", "submodules"}
               for part in path.relative_to(GAMES).parts):
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if any(pattern.search(line) for pattern in PATTERNS):
                failures.append(f"{path.relative_to(ROOT)}:{number}: {line.strip()}")
    return failures


if __name__ == "__main__":
    issues = check()
    if issues:
        print("\n".join(issues), file=sys.stderr)
        raise SystemExit(f"Found {len(issues)} direct Emscripten references in game sources")
    print("[Kotonoha] All game sources and CMake files are Emscripten-independent")
