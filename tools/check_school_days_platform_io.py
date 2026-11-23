#!/usr/bin/env python3
"""Guard engine-owned persistence and the default launcher for School Days."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SCHOOL = ROOT / "games/school-days"
ENGINE = ROOT / "lib/src/filesystem/AtomicFile.cpp"


def inspect() -> list[str]:
    issues: list[str] = []
    writer = (SCHOOL / "src/SchoolDaysAtomicFile.cpp").read_text()
    storage = (SCHOOL / "src/SchoolDaysSaveStorage.cpp").read_text()
    main = (SCHOOL / "src/main.cpp").read_text()
    engine = ENGINE.read_text()
    header = (ROOT / "lib/include/Kotonoha/Filesystem/AtomicFile.hpp").read_text()
    for name, data in (("SchoolDaysAtomicFile.cpp", writer),
                       ("SchoolDaysSaveStorage.cpp", storage),
                       ("engine AtomicFile.cpp", engine)):
        for operation in ("windows.h", "CreateFileW", "MoveFileExW",
                          "GetCurrentProcessId", "OpenProcess", "WriteFile(",
                          "FlushFileBuffers", "SetFileAttributesW", "unistd.h",
                          "sys/stat.h", "fcntl.h", "::open(", "::write(",
                          "::fsync(", "::rename(", "::getpid(", "::kill("):
            if operation in data:
                issues.append(f"{name}: direct OS I/O is forbidden ({operation})")
    for symbol in ("Filesystem::writeBinaryFileAtomically",
                   "maxGlobalFlagBytes"):
        if symbol not in writer:
            issues.append(f"School Days writer does not use shared policy: {symbol}")
    for symbol in ("Filesystem::readLocalFile", "Filesystem::SDLFileSource",
                   "Filesystem::OutputFile"):
        if symbol not in storage:
            issues.append(f"School Days save I/O bypasses shared adapter: {symbol}")
    for operation in ("SDL_IOFromFile", "SDL_WriteIO", "SDL_FlushIO",
                      "SDL_RenamePath", "SDL_RemovePath"):
        if operation not in engine:
            issues.append(f"Missing shared SDL atomic operation: {operation}")
    if "hardware-power-loss" not in header:
        issues.append("Document the filesystem durability limitation")
    before = main.split("arguments.parse(argc, argv);", 1)[0]
    if "app->options.launcher = false" in before:
        issues.append("Launcher is disabled before command-line options are parsed")
    if not re.search(r"arguments\.frames\s*>\s*0", main):
        issues.append("Headless launcher bypass is missing")
    if "app->launcher->open()" not in main:
        issues.append("School Days does not open its shared game launcher")
    return issues


if __name__ == "__main__":
    errors = inspect()
    for problem in errors:
        print("[Platform IO] " + problem, file=sys.stderr)
    if errors:
        raise SystemExit(1)
    print("[Platform IO] Shared persistence and launcher contracts passed")
