#!/usr/bin/env python3
"""Read-only consistency check for CMake and Xcode metadata (Python 3.8+)."""
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
XCODE = ROOT / "buildsystems" / "xcode"
SCHEMES = XCODE / "Kotonoha.xcodeproj" / "xcshareddata" / "xcschemes"
VISUAL_STUDIO = ROOT / "buildsystems" / "visualstudio"


def require(value, message):
    if not value:
        raise ValueError(message)


def validate_visual_studio(xcode_manifest):
    manifest = json.loads((VISUAL_STUDIO / "ProjectManifest.json").read_text(encoding="utf-8"))
    require(manifest["project"] == "buildsystems/visualstudio/Kotonoha.sln",
            "Unexpected Visual Studio solution path")
    require(set(manifest["platforms"]) == {"Win32", "x64", "ARM64"},
            "Visual Studio platforms must be Win32/x64/ARM64")
    require(set(manifest["configurations"]) == {"Debug", "Release"},
            "Visual Studio configurations must be Debug/Release")
    require(manifest["architectures"] == {"x86": "Win32", "amd64": "x64", "arm64": "ARM64"},
            "Visual Studio architecture mapping disagrees with the platform matrix")
    require(manifest["vcpkg_triplets"] == {"Win32": "x86-windows",
                                           "x64": "x64-windows",
                                           "ARM64": "arm64-windows"},
            "Visual Studio vcpkg triplets disagree with the platform matrix")
    for doc in ("README.md", "README.pt-BR.md"):
        require((VISUAL_STUDIO / doc).is_file(), f"Missing Visual Studio {doc}")
    for doc in ("VISUAL_STUDIO_TARGETS.md", "VISUAL_STUDIO_TARGETS.pt-BR.md"):
        require((ROOT / "docs" / doc).is_file(), f"Missing {doc}")

    targets = {target["name"]: target for target in manifest["targets"]}
    expected_games = set(xcode_manifest["games"])
    actual_games = {name for name, target in targets.items() if "game" in target}
    require(expected_games == actual_games,
            "Visual Studio game targets differ from Xcode ProjectManifest")
    for name, slug in xcode_manifest["games"].items():
        require(targets[name]["game"] == slug, f"Wrong game slug for {name}")
    for name in ("KotonohaEngine", "KotonohaImGui", "KotonohaLauncher"):
        require(name in targets, f"Missing Visual Studio target: {name}")

    sln = (VISUAL_STUDIO / "Kotonoha.sln").read_text(encoding="utf-8")
    combos = [f"{cfg}|{plat}" for cfg in manifest["configurations"]
              for plat in manifest["platforms"]]
    for name, target in targets.items():
        project = VISUAL_STUDIO / "Projects" / name / (name + ".vcxproj")
        require(project.is_file(), f"Missing Visual Studio project: {name}.vcxproj")
        text = project.read_text(encoding="utf-8")
        guids = re.findall(r"<ProjectGuid>([^<]+)</ProjectGuid>", text)
        require(len(guids) == 1 and guids[0] in sln,
                f"Project GUID missing from solution: {name}")
        for combo in combos:
            require(f'<ProjectConfiguration Include="{combo}">' in text,
                    f"{name}: missing {combo}")
        for source in target["sources"]:
            require((ROOT / source).is_file(), f"{name}: missing source {source}")

    for native, info in manifest["native_projects"].items():
        require((ROOT / info["path"]).is_file(), f"Missing native project: {native}")
        require(info["guid"] in sln, f"Native project absent from solution: {native}")
        require(set(info["platforms"]) <= set(manifest["platforms"]),
                f"Unknown platform for native project: {native}")

    folder_type = "{2150E333-8FDC-42A3-9474-1A3956D46DE8}"
    for folder in ("lib", "games", "modules", "launcher"):
        require(f'Project("{folder_type}") = "{folder}"' in sln,
                f"Missing solution folder: {folder}")
    require("GlobalSection(NestedProjects) = preSolution" in sln,
            "Solution folders are not wired up")
    print(f"[Kotonoha] OK: {len(targets)} Visual Studio projects; "
          f"{len(manifest['native_projects'])} imported projects; "
          f"{len(combos)} configuration/platform targets")


def validate():
    manifest = json.loads((XCODE / "ProjectManifest.json").read_text(encoding="utf-8"))
    # All native macOS app targets invoke ConfigureMacOSIcon.sh, which reads
    # the versioned design/generated/ source of truth. Ensure the real binary
    # resource exists, not just a reference in ProjectManifest.
    icon = (ROOT / "design" / "generated" / "generic" / "icon.icns").read_bytes()
    require(len(icon) >= 8 and icon[:4] == b"icns" and
            int.from_bytes(icon[4:8], "big") == len(icon),
            "Missing or malformed design/generated/generic/icon.icns")

    root = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    modules = ("KotonohaGames", "KotonohaAndroid", "KotonohaOptions", "KotonohaReport")
    positions = [root.index(name + ".cmake") for name in modules]
    require(positions == sorted(positions), "CMake includes must remain ordered")
    require(root.index("KotonohaAndroid.cmake") < root.index("project(Kotonoha"),
            "Android toolchain selection must precede project()")
    matrix = (ROOT / "buildsystems/cmake/games/KotonohaGames.cmake").read_text(encoding="utf-8")
    rows = re.findall(r'"([A-Z0-9_]+)\|([^|"\n]+)\|(games/[^"\n]+)"', matrix)
    require(len(rows) == len(manifest["games"]), "Incorrect number of CMake games")
    require({path: slug for _, slug, path in rows} ==
            {"games/" + slug: slug for slug in manifest["games"].values()},
            "CMake game IDs differ from Xcode ProjectManifest")
    require(len({option for option, _, _ in rows}) == len(rows),
            "Duplicate CMake build option")
    options = (ROOT / "buildsystems/cmake/KotonohaOptions.cmake").read_text(encoding="utf-8")
    require("KOTONOHA_BUILD_GAME" in options and "KOTONOHA_BUILD_" in options,
            "Missing CMake game selection")

    for config in (XCODE / "Config").glob("*.xcconfig"):
        content = config.read_text(encoding="utf-8")
        for inc in re.findall(r'^\s*#include\s+"([^"]+)"', content, re.M):
            require((config.parent / inc).is_file(), f"{config.name}: missing {inc}")

    pbx = (XCODE / "Kotonoha.xcodeproj/project.pbxproj").read_text(encoding="utf-8")
    expected = {(platform, name): data["target"]
                for key, data in manifest["targets"].items()
                for platform, name in [key.split("/", 1)]
                if name not in ("KotonohaEngine", "KotonohaImGui")}
    # A CMake/Android port is NOT automatically an Xcode scheme. Missing
    # macOS/iOS schemes must be explicitly reported and may not silently be
    # counted as Apple-compatible.
    pending_xcode = set()
    for cmake_target, slug in manifest["games"].items():
        platforms = {platform for platform in ("macOS", "iOS")
                     if (platform, cmake_target) in expected}
        if not platforms:
            pending_xcode.add(slug)
        else:
            require(platforms == {"macOS", "iOS"},
                    f"Partial Xcode target for {slug}: {sorted(platforms)}")
    require(pending_xcode == set(manifest.get("xcode_pending", [])),
            f"Untracked Xcode scheme gap: {sorted(pending_xcode)}")

    schemes = list(SCHEMES.glob("*.xcscheme"))
    require(len(schemes) == len(expected) == manifest["schemes"],
            "Incorrect number of Xcode schemes")
    seen = set()
    for scheme in schemes:
        match = re.fullmatch(r"(.+) \((macOS|iOS)\)\.xcscheme", scheme.name)
        require(match, f"Unexpected/duplicate scheme: {scheme.name}")
        name, platform = match.groups()
        key = platform, name
        require(key in expected and key not in seen, f"Unexpected scheme: {scheme.name}")
        seen.add(key)
        xml = ET.parse(scheme).getroot()
        refs = xml.findall(".//BuildableReference")
        require(refs, f"Missing Xcode buildable reference: {scheme.name}")
        for ref in refs:
            require(ref.get("ReferencedContainer") == "container:Kotonoha.xcodeproj",
                    f"Stale Xcode project reference: {scheme.name}")
            require(ref.get("BlueprintIdentifier") == expected[key],
                    f"Incorrect target ID: {scheme.name}")
        require(expected[key] in pbx, f"Target absent from project.pbxproj: {scheme.name}")
        actions = xml.findall(".//BuildAction/PreActions/ExecutionAction/ActionContent")
        require(any("PrepareApple.py" in action.get("scriptText", "") for action in actions),
                f"Missing preparation step: {scheme.name}")
    require(seen == set(expected), "Some Xcode schemes are missing")
    print(f"[Kotonoha] OK: {len(rows)} CMake games; {len(schemes)} existing Xcode "
          f"schemes; Xcode pending: {', '.join(sorted(pending_xcode)) or 'none'}")
    validate_visual_studio(manifest)


if __name__ == "__main__":
    try:
        validate()
    except (ValueError, OSError, ET.ParseError) as exc:
        print(f"[Kotonoha] Validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
