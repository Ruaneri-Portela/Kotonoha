#!/usr/bin/env python3
"""Read-only Windows L4 CMake and source-safety audit for this Kotonoha fork.

The upstream root registers games through KotonohaGames/KotonohaOptions; this
Windows-only audit retains the fork's School Days asset and Git boundaries.
It intentionally does NOT test Android, Xcode/iOS, macOS, Linux or Web.
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
    matrix = source(ROOT / "buildsystems" / "cmake" / "KotonohaGames.cmake")
    options_file = source(ROOT / "buildsystems" / "cmake" / "KotonohaOptions.cmake")
    rows = re.findall(
        r'"([A-Z_]+)\|([^|"\n]+)\|(games/[^"\n]+)"', matrix
    )
    require(rows and len(rows) == len({x[0] for x in rows}),
            "Invalid or duplicate upstream CMake game matrix")
    require(len(rows) == len({x[1] for x in rows}) ==
            len({x[2] for x in rows}),
            "Duplicate game IDs or directories in upstream game matrix")
    require("option(KOTONOHA_BUILD_${game_option}" in options_file,
            "Upstream game options are no longer defined by the matrix")
    require("if(KOTONOHA_BUILD_${game_option})" in root and
            'add_subdirectory("${game_directory}")' in root,
            "Root must use upstream game matrix to register games")
    require(root.index("KotonohaAndroid.cmake") < root.index("project(Kotonoha"),
            "Android toolchain entry must precede project()")
    for name in ("KotonohaGames", "KotonohaAndroid", "KotonohaOptions",
                 "KotonohaReport", "KotonohaCPack"):
        require(name + ".cmake" in root, f"Missing upstream CMake module {name}")
    for _, _, relative in rows:
        require((ROOT / relative / "CMakeLists.txt").is_file(),
                f"Missing registered CMake game: {relative}")
    require("option(KOTONOHA_DISTRIBUTION_BUILD" in root and
            "option(KOTONOHA_DEV_CONTROL" in root and
            "option(KOTONOHA_SCHOOL_DAYS_RETAIL_ONLY" in root,
            "Missing inherited Windows L4 DEV/DIST/retail-only build gates")

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
    print(f"[Kotonoha] OK: Windows L4 metadata; {len(rows)} game options; "
          f"{len(sources)} School Days sources; {tracked_count} tracked paths checked")
    print("[Kotonoha] Skipped: Android, iOS, macOS, Linux, Web metadata")


if __name__ == "__main__":
    try:
        validate()
    except (ValueError, OSError, UnicodeError,
            subprocess.CalledProcessError) as error:
        print(f"[Kotonoha] Validation failed: {error}", file=sys.stderr)
        sys.exit(1)
