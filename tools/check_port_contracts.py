#!/usr/bin/env python3
"""Check shared startup, bundle-ID, asset and CMake contracts of every game port.

This deliberately does not assert story/route parity or require commercial data.
Run with: python3 tools/check_port_contracts.py
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "buildsystems/xcode/ProjectManifest.json"


def errors():
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    issues = []
    for target, slug in manifest["games"].items():
        root = ROOT / "games" / slug
        main = root / "src/main.cpp"
        cmake = root / "CMakeLists.txt"
        readme = root / "README.md"
        tests = root / "tests"
        for path in (main, cmake, readme):
            if not path.is_file():
                issues.append(f"{slug}: missing {path.relative_to(ROOT)}")
        if not main.is_file() or not cmake.is_file():
            continue
        source = main.read_text(encoding="utf-8")
        config = cmake.read_text(encoding="utf-8")

        # The Xcode scheme is KotonohaExample, while its CMake executable
        # intentionally keeps the historical target name Kotonoha.
        cmake_target = "Kotonoha" if slug == "example" else target
        configured = re.search(
            r"\\bkotonoha_configure_app\\s*\\(\\s*" + re.escape(cmake_target) +
            r"\b[^)]*\bGAME\s+" + re.escape(slug) + r"\b", config, re.S
        )
        if not configured:
            issues.append(f"{slug}: CMake target or GAME slug differs from manifest")
        if "KOTONOHA_SDL_CALLBACK_APP(" not in source:
            issues.append(f"{slug}: missing shared SDL callback integration")
        if not tests.is_dir() or not any(tests.glob("*.cpp")):
            issues.append(f"{slug}: no C++ compatibility regression fixtures")

        bundle = re.search(r"\bBUNDLE_ID\s+([^\s)]+)", config)
        metadata = re.search(
            r'KOTONOHA_SET_APP_METADATA\(\s*"[^"]+"\s*,\s*"([^"]+)"',
            source, re.S
        )
        if metadata and (not bundle or metadata.group(1) != bundle.group(1)):
            issues.append(
                f"{slug}: SDL metadata ID {metadata.group(1)} disagrees with "
                f"CMake bundle identifier {bundle.group(1) if bundle else '<none>'}"
            )

        # All path lookups must use the same slug as CMake/Xcode packaging.
        for call, name in re.findall(
            r'\b(gameAssetDirectory|findGameAsset|findGamePath)\s*\(\s*"([^"]+)"',
            source
        ):
            if name != slug:
                issues.append(
                    f"{slug}: {call} references '{name}', expected '{slug}'"
                )
        if re.search(
            r'runtime\(std::move\(a\)\)\s*,\s*'
            r'engine\(runtime\s*,\s*std::move\(o\)\)',
            source,
        ):
            issues.append(
                f"{slug}: custom provider is moved into runtime but not engine"
            )
        if re.search(r'/(Volumes|Users|home)/[^"\s]+', source):
            issues.append(f"{slug}: machine-specific absolute game path in main.cpp")

    return issues, len(manifest["games"])


if __name__ == "__main__":
    try:
        findings, count = errors()
    except (OSError, ValueError, KeyError) as exc:
        print(f"[Port contracts] Cannot inspect manifests: {exc}", file=sys.stderr)
        sys.exit(1)
    if findings:
        for issue in findings:
            print(f"[Port contracts] {issue}", file=sys.stderr)
        sys.exit(1)
    print(f"[Port contracts] {count} game applications checked successfully")
