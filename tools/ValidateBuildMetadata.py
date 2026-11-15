#!/usr/bin/env python3
"""Read-only Windows L4 CMake metadata check for this Kotonoha fork.

Adapted from Ruaneri's ValidateBuildMetadata.py. Unlike upstream, this fork's
root CMake still selects game targets directly and has separate legacy CMake
modules. This intentionally does NOT validate or run Android, Xcode, Linux,
macOS, iOS or Web configurations; those need separate authorized reviews.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCHOOL_DAYS = ROOT / "games" / "school-days"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def source(path: Path) -> str:
    require(path.is_file(), f"Missing build metadata: {path.relative_to(ROOT)}")
    return path.read_text(encoding="utf-8")


def validate_git_safety() -> int:
    result = subprocess.run(
        ["git", "ls-files", "-z"], cwd=ROOT, check=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    tracked = [p.decode("utf-8") for p in result.stdout.split(b"\0") if p]
    allowlisted_assets = {
        "games/school-days/game-assets/.gitignore",
        "games/school-days/game-assets/README.txt",
        "games/school-days/game-assets/school-days-hq.ktnroute",
        "games/school-days/game-assets/styles.ass",
    }
    forbidden_suffixes = (
        ".gpk", ".iso", ".img", ".vhd", ".vhdx", ".apk",
        ".exe", ".msi", ".zip", ".7z", ".rar", ".pfx",
        ".jks", ".keystore", ".pem",
    )
    for path in tracked:
        lower = path.lower()
        parts = lower.split("/")
        require(not lower.endswith(forbidden_suffixes),
                f"Forbidden tracked binary/archive: {path}")
        require(parts[0] not in ("build", "dist", "translations"),
                f"Forbidden generated/private directory tracked: {path}")
        require("packs" not in parts and "save" not in parts,
                f"Forbidden retail/save directory tracked: {path}")
        if lower.startswith("games/school-days/game-assets/"):
            require(path in allowlisted_assets,
                    f"Non-allowlisted School Days asset tracked: {path}")
    ignores = source(ROOT / ".gitignore")
    require("*.[Gg][Pp][Kk]" in ignores and "/translations/" in ignores,
            "Private game archives and translations must remain ignored")
    return len(tracked)


def validate() -> None:
    root = source(ROOT / "CMakeLists.txt")
    game = source(SCHOOL_DAYS / "CMakeLists.txt")
    options = re.findall(r"option\(KOTONOHA_BUILD_([A-Z_]+)\b", root)
    registrations = re.findall(
        r"if\(KOTONOHA_BUILD_([A-Z_]+)\)\s*"
        r"add_subdirectory\((games/[^)]+)\)", root,
    )
    require(options and len(options) == len(set(options)),
            "Missing or duplicate KOTONOHA_BUILD_* options")
    require({option for option, _ in registrations} == set(options),
            "CMake game options and guarded game directories differ")
    require(len(registrations) == len({path for _, path in registrations}),
            "Duplicate CMake game directory registration")
    for _, relative in registrations:
        require((ROOT / relative / "CMakeLists.txt").is_file(),
                f"Missing registered CMake game: {relative}")

    require("target_compile_features(school_days PUBLIC cxx_std_17)" in game,
            "School Days target must remain C++17")
    library = re.search(r"add_library\(school_days STATIC\s*(.*?)\n\)",
                        game, re.S)
    require(library is not None, "Missing School Days static target")
    sources = re.findall(r"\bsrc/[A-Za-z0-9_/]+\.cpp\b", library.group(1))
    require(sources and len(sources) == len(set(sources)),
            "Missing or duplicated School Days C++ source list")
    for item in sources + ["src/main.cpp"]:
        require((SCHOOL_DAYS / item).is_file(),
                f"Missing School Days CMake source: {item}")

    require("if(WIN32 AND NOT ANDROID)" in game,
            "Windows artwork staging must be platform-gated")
    require("game-assets/launcher-art/SchoolDaysHQ-Launcher.png" in game,
            "Launcher source path is not the approved private asset location")
    require("game-assets/display-options" in game,
            "Display Options source path is not the approved private overlay")
    require("SchoolDaysLauncherArtwork" in game and
            "SchoolDaysOptionAssets" in game and
            "StageOptionalAssets.cmake" in game,
            "Expected Windows artwork staging targets are missing")
    stage = source(SCHOOL_DAYS / "windows" / "StageOptionalAssets.cmake")
    require("message(WARNING" in stage and "copy_if_different" in stage and
            "copy_directory" in stage,
            "Artwork staging must warn on missing input and copy both kinds")
    require((SCHOOL_DAYS / "windows" / "SchoolDays.rc").is_file(),
            "Missing Windows icon resource declaration")
    require((SCHOOL_DAYS / "tests" / "CMakeLists.txt").is_file(),
            "Missing School Days test registration")

    for base in (
        ROOT / "docs" / "README",
        ROOT / "docs" / "MAINTENANCE_BACKLOG",
        SCHOOL_DAYS / "docs" / "README",
        SCHOOL_DAYS / "docs" / "configuration" / "display-options" / "README",
    ):
        english = base.with_suffix(".md")
        portuguese = base.with_name(base.name + ".pt-BR.md")
        require(english.is_file() and portuguese.is_file(),
                f"Missing bilingual pair: {base.relative_to(ROOT)}")
        require(portuguese.name in source(english),
                f"Missing cross-language README link: {english.relative_to(ROOT)}")

    tracked_count = validate_git_safety()
    print(f"[Kotonoha] OK: Windows L4 metadata; {len(options)} game options; "
          f"{len(sources)} School Days sources; {tracked_count} tracked paths checked")
    print("[Kotonoha] Skipped: Android, iOS, macOS, Linux, Web metadata")


if __name__ == "__main__":
    try:
        validate()
    except (ValueError, OSError, UnicodeError,
            subprocess.CalledProcessError) as error:
        print(f"[Kotonoha] Validation failed: {error}", file=sys.stderr)
        sys.exit(1)
