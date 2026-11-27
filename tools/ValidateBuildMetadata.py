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
            # Framework targets may be explicit build-action dependencies of
            # the launcher scheme; they are not independent schemes.
            if ref.get("BlueprintName", "").startswith("KotonohaLauncherCore-"):
                continue
            require(ref.get("BlueprintIdentifier") == expected[key],
                    f"Incorrect target ID: {scheme.name}")
        require(expected[key] in pbx, f"Target absent from project.pbxproj: {scheme.name}")
        actions = xml.findall(".//BuildAction/PreActions/ExecutionAction/ActionContent")
        require(any("PrepareApple.py" in action.get("scriptText", "") for action in actions),
                f"Missing preparation step: {scheme.name}")
    require(seen == set(expected), "Some Xcode schemes are missing")

    # Visual Studio solution: same read-only consistency guarantees as Xcode.
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import check_visual_studio_sources as vs  # noqa: E402
    vs.validate()

    # Build-script convention: Windows uses PowerShell only; macOS/Linux use
    # Python or sh. WASM helpers live under tools/, never at the repository root.
    vs_dir = ROOT / "buildsystems" / "visualstudio"
    vs_sources = [p for p in vs_dir.rglob("*")
                  if p.is_file() and not any(part in ("out", "obj", "vcpkg")
                                             for part in p.relative_to(vs_dir).parts)]
    require(not any(p.suffix in (".sh", ".bat", ".cmd", ".py") for p in vs_sources),
            "Visual Studio build scripts must be PowerShell (.ps1) only")
    for name in ("build-wasm.sh", "build-wasm.ps1"):
        require(not (ROOT / name).exists(), f"{name} must live under tools/")
        require((ROOT / "tools" / name).is_file(), f"Missing tools/{name}")
    require(not any(p.suffix in (".bat", ".cmd")
                    for p in (ROOT / "tools").glob("build-*.*")),
            "Windows build helpers under tools/ must be .ps1")

    print(f"[Kotonoha] OK: {len(rows)} CMake games; {len(schemes)} existing Xcode "
          f"schemes; Xcode pending: {', '.join(sorted(pending_xcode)) or 'none'}")


if __name__ == "__main__":
    try:
        validate()
    except (ValueError, OSError, ET.ParseError) as exc:
        print(f"[Kotonoha] Validation failed: {exc}", file=sys.stderr)
        sys.exit(1)
