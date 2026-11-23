#!/usr/bin/env python3
"""Guard engine-owned durable persistence and School Days launcher contracts."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SCHOOL = ROOT / "games/school-days"
ENGINE = ROOT / "lib/src/filesystem/AtomicFile.cpp"


def inspect() -> list[str]:
    issues: list[str] = []
    writer = (SCHOOL / "src/SchoolDaysAtomicFile.cpp").read_text(encoding="utf-8")
    storage = (SCHOOL / "src/SchoolDaysSaveStorage.cpp").read_text(encoding="utf-8")
    main = (SCHOOL / "src/main.cpp").read_text(encoding="utf-8")
    engine = ENGINE.read_text(encoding="utf-8")
    header = (ROOT / "lib/include/Kotonoha/Filesystem/AtomicFile.hpp").read_text(
        encoding="utf-8"
    )

    # OS-specific durability belongs to the generic shared filesystem only.
    # Check the game owner and its storage adapter, not the engine implementation.
    native_operations = (
        "windows.h", "Windows.h", "CreateFileW", "MoveFileExW", "GetCurrentProcessId",
        "OpenProcess", "WriteFile(", "FlushFileBuffers", "GetFileAttributesW",
        "SetFileAttributesW", "unistd.h", "sys/stat.h", "fcntl.h",
        "::open(", "::write(", "::fsync(", "::rename(",
        "::getpid(", "::kill(",
    )
    for name, data in (
        ("SchoolDaysAtomicFile.cpp", writer),
        ("SchoolDaysSaveStorage.cpp", storage),
    ):
        for operation in native_operations:
            if operation in data:
                issues.append(
                    f"{name}: direct OS I/O is forbidden in game code ({operation})"
                )

    for symbol in (
        "Filesystem::writeBinaryFileAtomically",
        "maxGlobalFlagBytes",
        "Filesystem::AtomicFileFault::DirectoryFlushBefore",
        "Filesystem::AtomicFileFault::DirectoryFlushAfter",
    ):
        if symbol not in writer:
            issues.append(f"School Days writer does not use shared policy: {symbol}")
    for symbol in (
        "Filesystem::readLocalFile",
        "Filesystem::SDLFileSource",
        "Filesystem::OutputFile",
    ):
        if symbol not in storage:
            issues.append(f"School Days save I/O bypasses shared adapter: {symbol}")

    # Native branches must provide more than SDL_FlushIO / SDL_RenamePath:
    # file and directory durability, replacement semantics and orphan hygiene.
    durable_windows = (
        "#elif defined(_WIN32)",
        "CreateFileW", "CREATE_NEW", "FILE_FLAG_WRITE_THROUGH",
        "WriteFile(", "FlushFileBuffers", "CloseHandle", "MoveFileExW",
        "MOVEFILE_REPLACE_EXISTING", "MOVEFILE_WRITE_THROUGH",
        "ERROR_SHARING_VIOLATION", "FILE_ATTRIBUTE_READONLY",
    )
    durable_posix = (
        "::open(", "O_EXCL", "::write(", "::fsync(", "::rename(",
        "O_DIRECTORY", "syncDirectory(", "durableFileSync(",
        "F_FULLFSYNC", "EINTR",
    )
    safe_publication = (
        "symlink_status", "fs::is_symlink", "ownerDefinitelyDead",
        "orphanMinimumAge", "std::chrono::hours(24)", "pending.owned",
        "pending.retain", "SimulatedInterruption",
        "DirectoryFlushBefore", "DirectoryFlushAfter",
    )
    wasm_fallback = (
        "#if defined(__EMSCRIPTEN__)",
        "SDL_IOFromFile", "SDL_WriteIO", "SDL_FlushIO", "SDL_CloseIO",
        "SDL_RenamePath", "SDL_RemovePath",
    )
    for name, symbols in (
        ("native Windows durability", durable_windows),
        ("native POSIX durability", durable_posix),
        ("safe atomic publication", safe_publication),
        ("Emscripten SDL fallback", wasm_fallback),
    ):
        for symbol in symbols:
            if symbol not in engine:
                issues.append(f"Shared {name} requirement missing: {symbol}")
    if "hardware-power-loss" not in header:
        issues.append("Document Web/IDBFS hardware-power-loss limitation")
    if "Emscripten" not in header or "fsync" not in header:
        issues.append("Document shared POSIX and Emscripten durability contracts")

    # Keep upstream launcher constraints unchanged.
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
    print("[Platform IO] Shared durable persistence and launcher contracts passed")
