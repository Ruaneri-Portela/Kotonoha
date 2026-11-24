#!/usr/bin/env python3
"""Source-only contracts for the standalone hub and modular game entries.

This checks names/IDs and target ownership, not whether retail games run.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LAUNCHER = ROOT / "launcher/core/UnifiedLauncher.cpp"


def audit() -> list[str]:
    issues: list[str] = []
    manifest = json.loads(
        (ROOT / "buildsystems/xcode/ProjectManifest.json").read_text(
            encoding="utf-8"))
    games = manifest["games"]
    source = LAUNCHER.read_text(encoding="utf-8")

    registry_pattern = re.compile(
        r'registerLibrary\(\{\s*EngineKind::\w+,\s*"(?P<slug>[^"]+)",'
        r'\s*"[^"]+",\s*"[^"]+",\s*"(?P<target>[^"]+)",'
        r'\s*"(?P<flag>[^"]*)",\s*"(?P<bundle>[^"]+)"',
        re.S)
    registry = {}
    for match in registry_pattern.finditer(source):
        slug = match.group("slug")
        if slug in registry:
            issues.append(f"Duplicate launcher registry entry for {slug}")
        registry[slug] = match.groupdict()
    # Generic Ren'Py is a virtual launcher identity sharing the DDLC VM,
    # not another standalone game application in the Xcode/Android matrix.
    expected_slugs = (set(games.values()) - {"example"}) | {"renpy"}
    if registry.get("renpy", {}).get("target") != "DokiDoki" or (
        registry.get("renpy", {}).get("bundle") !=
        "me.hirameki.kotonoha.renpy"
    ):
        issues.append("Generic Ren'Py registry identity is inconsistent")
    if "example" in registry:
        issues.append("Example must not be registered in the launcher")
    if set(registry) != expected_slugs:
        issues.append(
            "Launcher registration and game manifest differ: " +
            f"missing={sorted(expected_slugs - set(registry))}, " +
            f"extra={sorted(set(registry) - expected_slugs)}")

    android_text = (
        ROOT / "buildsystems/android/app/build.gradle").read_text(
            encoding="utf-8")
    try:
        android_matrix = android_text.split("def games = [", 1)[1].split(
            "\n]", 1)[0]
    except IndexError:
        return issues + ["Android games matrix not found"]
    android_games = {}
    for slug, target, bundle in re.findall(
        r"\['([^']+)',\s*'([^']+)',\s*'([^']+)'", android_matrix
    ):
        android_games[slug] = (target, bundle)

    for target, slug in games.items():
        cmake = (ROOT / "games" / slug / "CMakeLists.txt").read_text(
            encoding="utf-8")
        main = (ROOT / "games" / slug / "src/main.cpp").read_text(
            encoding="utf-8")
        binary = "Kotonoha" if slug == "example" else target
        declared_binary = re.search(
            r"\bkotonoha_add_app\(\s*([A-Za-z0-9_]+)", cmake)
        declared_bundle = re.search(r"\bBUNDLE_ID\s+([^\s)]+)", cmake)
        bundle = declared_bundle.group(1) if declared_bundle else None
        if not declared_binary or declared_binary.group(1) != binary:
            issues.append(f"{slug}: unexpected CMake executable")
        if not bundle:
            issues.append(f"{slug}: missing CMake bundle ID")

        entry = registry.get(slug)
        if entry:
            if entry["target"] != binary:
                issues.append(f"{slug}: hub executable differs from CMake")
            if entry["bundle"] != bundle:
                issues.append(f"{slug}: hub bundle ID differs from CMake")
            flag = entry["flag"]
            if flag and not re.search(
                r'\.option\(\s*"' + re.escape(flag) + '"', main
            ):
                issues.append(f"{slug}: hub CLI option {flag} is not registered")

        if android_games.get(slug) != (binary, bundle):
            issues.append(f"{slug}: Android target/application ID differs from CMake")

    if "/Users/nero/" in source or "/Volumes/Nero" in source:
        issues.append("Standalone launcher contains a machine-specific path")
    # Every selected port's library must be part of the launcher link graph.
    cmake = (ROOT / "launcher/CMakeLists.txt").read_text(encoding="utf-8")
    factories = (ROOT / "launcher/core/GameFactories.cpp").read_text(
        encoding="utf-8")
    hub_main = (ROOT / "launcher/app/main.cpp").read_text(encoding="utf-8")
    if '"EXAMPLE|' in cmake or "KOTONOHA_LAUNCH_EXAMPLE" in factories:
        issues.append("The launcher must exclude the Example demo")
    if "KOTONOHA_BUILD_ALL" not in cmake:
        issues.append("The launcher CMake target must require BUILD_ALL")
    # KotonohaLauncherCore is the canonical reusable library; the old
    # kotonoha_unified_launcher name remains as a CMake alias for consumers.
    core_link = ("target_link_libraries(KotonohaLauncherCore PUBLIC" in cmake or
                 "target_link_libraries(kotonoha_unified_launcher PUBLIC" in cmake)
    if "core/GameFactories.cpp" not in cmake or not core_link:
        issues.append("Game factories are not statically linked into the hub")
    if "add_library(kotonoha_unified_launcher ALIAS KotonohaLauncherCore)" not in cmake:
        issues.append("Legacy kotonoha_unified_launcher alias is missing")
    for option, slug, _ in re.findall(
        r'"([A-Z0-9_]+)\|([^|"\n]+)\|(games/[^"\n]+)"',
        (ROOT / "buildsystems/cmake/games/KotonohaGames.cmake").read_text(
            encoding="utf-8")):
        if option == "EXAMPLE":
            continue
        if f"KOTONOHA_LAUNCH_{option}" not in factories:
            issues.append(f"{slug}: no linked C++ runtime factory")
        if f'"{option}|' not in cmake:
            issues.append(f"{slug}: no launcher CMake link entry")
    for token in ("registerLinkedEngineFactories", "MountedEngineInstance"):
        if token not in factories or token not in (factories + source):
            issues.append(f"Missing linked runtime support: {token}")
    for token in ("startInProcess(", "gameEnvironment", "stopGame()"):
        if token not in hub_main:
            issues.append(f"Missing in-process game lifecycle step: {token}")
    for forbidden in ("launchModularPortProcess(", "fork(", "execv(",
                      "_wspawnv(", "system(", "popen("):
        if forbidden in source or forbidden in hub_main:
            issues.append(f"Process spawning is forbidden in hub: {forbidden}")

    n2 = (ROOT / "games/n2system/CMakeLists.txt").read_text(encoding="utf-8")
    if 'PonscripterLibrary.cmake' not in n2 or (
        "../ponscripter\n" in n2 or "PonscripterGame" in n2
    ):
        issues.append("N2System must reuse only the Ponscripter library")

    root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    games_cmake = (ROOT / "buildsystems/cmake/games/KotonohaBuild.cmake").read_text(
        encoding="utf-8")
    # The hub app is desktop-only (Android's Material launcher uses the core via
    # JNI), while KotonohaLauncherCore is also configured for Emscripten to host
    # the JS bridge.
    if "games/KotonohaBuild.cmake" not in root_cmake or (
        "(KOTONOHA_BUILD_ALL OR KOTONOHA_BUILD_LAUNCHER) AND"
        not in games_cmake or "add_subdirectory(launcher)" not in games_cmake
    ):
        issues.append("Native/Android-only standalone launcher guard is missing")
    return issues


if __name__ == "__main__":
    try:
        failures = audit()
    except (OSError, ValueError, KeyError) as error:
        print(f"[Launcher contracts] Cannot inspect project: {error}",
              file=sys.stderr)
        raise SystemExit(1)
    for finding in failures:
        print(f"[Launcher contracts] {finding}", file=sys.stderr)
    if failures:
        raise SystemExit(1)
    print("[Launcher contracts] All registered game runtimes match their "
          "CMake/Android identities; Example excluded; BUILD_ALL required")
