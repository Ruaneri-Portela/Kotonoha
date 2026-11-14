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


def require(value, message):
    if not value:
        raise ValueError(message)


def validate():
    manifest = json.loads((XCODE / "ProjectManifest.json").read_text(encoding="utf-8"))
    root = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    modules = ("KotonohaGames", "KotonohaAndroid", "KotonohaOptions", "KotonohaReport")
    positions = [root.index(name + ".cmake") for name in modules]
    require(positions == sorted(positions), "CMake includes must remain ordered")
    require(root.index("KotonohaAndroid.cmake") < root.index("project(Kotonoha"),
            "Android toolchain selection must precede project()")
    matrix = (ROOT / "buildsystems/cmake/KotonohaGames.cmake").read_text(encoding="utf-8")
    rows = re.findall(r'"([A-Z_]+)\|([^|"\n]+)\|(games/[^"\n]+)"', matrix)
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
    print(f"[Kotonoha] OK: {len(rows)} games; {len(schemes)} schemes; CMake/Xcode consistent")


if __name__ == "__main__":
    try:
        validate()
    except (ValueError, OSError, ET.ParseError) as exc:
        print(f"[Kotonoha] Validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
