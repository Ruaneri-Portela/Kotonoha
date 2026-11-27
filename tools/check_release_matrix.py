#!/usr/bin/env python3
"""Verify that game ports cannot silently disappear from release packaging.

This is a source-level contract: it does not claim that a native binary or APK
has been compiled. Artifact counts are checked by their platform workflows.
"""
from __future__ import annotations

import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
# The fork also carries an experimental AIR Standard Edition port that is
# configured as a standalone CMake project, not a canonical launcher/flavor
# entry. Do not silently expand the upstream release matrix to distribute it.
STANDALONE_UNRELEASED_PORTS = {"air"}


def audit() -> list[str]:
    problems: list[str] = []
    manifest = json.loads(
        (ROOT / "buildsystems/xcode/ProjectManifest.json").read_text(
            encoding="utf-8"))
    canonical = manifest["games"]
    cmake = (ROOT / "buildsystems/cmake/games/KotonohaGames.cmake").read_text(encoding="utf-8")
    cmake_rows = re.findall(
        r'"([A-Z0-9_]+)\|([^|"\n]+)\|(games/[^"\n]+)"', cmake)
    cmake_slugs = {slug for _, slug, _ in cmake_rows}
    expected_slugs = set(canonical.values())
    # Detect newly added games that have a CMakeLists.txt but were never
    # registered in the canonical build / Android / packaging matrices.
    discovered_slugs = {
        folder.parent.name
        for folder in (ROOT / "games").glob("*/CMakeLists.txt")
    }
    if discovered_slugs - STANDALONE_UNRELEASED_PORTS != expected_slugs:
        problems.append(
            "Unregistered game directories: " +
            ", ".join(sorted((discovered_slugs - STANDALONE_UNRELEASED_PORTS) ^
                             expected_slugs)))
    if STANDALONE_UNRELEASED_PORTS & expected_slugs:
        problems.append("An unreleased standalone port entered the release matrix")
    if cmake_slugs != expected_slugs or len(cmake_rows) != len(expected_slugs):
        problems.append("CMake game matrix and ProjectManifest disagree")

    gradle = (ROOT / "buildsystems/android/app/build.gradle").read_text(
        encoding="utf-8")
    try:
        game_block = gradle.split("def games = [", 1)[1].split("\n]", 1)[0]
    except IndexError:
        return ["Android Gradle game matrix was not found"]
    android_rows = re.findall(
        r"(?m)^  ([A-Za-z][A-Za-z0-9_]*)\s*:\s*"
        r"\['([^']+)',\s*'([^']+)'", game_block)
    # The unified Android launcher is an additional application flavor,
    # not another entry in the canonical 15-game matrix.
    launcher_rows = [row for row in android_rows if row[0] == "launcher"]
    game_rows = [row for row in android_rows if row[0] != "launcher"]
    android_slugs = {slug for _, slug, _ in game_rows}
    if android_slugs != expected_slugs or len(game_rows) != len(expected_slugs):
        problems.append("Android flavors do not cover all CMake games exactly once")
    # The Material launcher flavor links only the JNI core; the SDL app target
    # (KotonohaLauncher) is a native/desktop-only binary.
    if launcher_rows != [("launcher", "all", "KotonohaLauncherCore")]:
        problems.append("Android launcher flavor must be launcher/all/KotonohaLauncherCore")
    expected_targets = {
        slug: ("Kotonoha" if slug == "example" else target)
        for target, slug in canonical.items()
    }
    for flavor, slug, target in game_rows:
        if expected_targets.get(slug) != target:
            problems.append(
                f"Android flavor {flavor}: target {target} differs from {slug}")

    for platform in ("linux", "macos", "windows"):
        workflow = (ROOT / f".github/workflows/{platform}.yml").read_text(
            encoding="utf-8")
        if 'ProjectManifest.json' not in workflow or (
            "len(packages) >= expected" not in workflow
        ):
            problems.append(f"{platform} packaging: count is not derived from game matrix")
        if "Expected 11 game packages" in workflow:
            problems.append(f"{platform} packaging uses the obsolete 11-port count")

    android_workflow = (
        ROOT / ".github/workflows/android.yml").read_text(encoding="utf-8")
    if android_workflow.count("flavor_entries = re.findall(") != 2:
        problems.append(
            "Android package and verification steps must both derive Gradle flavors")
    if "Build all 11 game APKs" in android_workflow:
        problems.append("Android build name still assumes 11 ports")
    return problems


if __name__ == "__main__":
    failures = audit()
    for failure in failures:
        print(f"[Release matrix] {failure}", file=sys.stderr)
    if failures:
        raise SystemExit(1)
    print("[Release matrix] Game slugs, native ZIPs, and Android flavors agree")
