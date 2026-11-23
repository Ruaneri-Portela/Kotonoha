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
            r"\bkotonoha_configure_app\s*\(\s*" + re.escape(cmake_target) +
            r"\b[^)]*\bGAME\s+" + re.escape(slug) + r"\b", config, re.S
        )
        if not configured:
            issues.append(f"{slug}: CMake target or GAME slug differs from manifest")
        if "KOTONOHA_SDL_CALLBACK_APP(" not in source:
            issues.append(f"{slug}: missing shared SDL callback integration")
        if not tests.is_dir() or not any(tests.glob("*.cpp")):
            issues.append(f"{slug}: no C++ compatibility regression fixtures")

        bundle = re.search(r"\bBUNDLE_ID\s+([^\s)]+)", config)
        metadata_ids = re.findall(
            r'KOTONOHA_SET_APP_METADATA\(\s*"[^"]+"\s*,\s*"([^"]+)"',
            source, re.S
        )
        allowed_ids = {bundle.group(1)} if bundle else set()
        allowed_paths = {slug}
        if slug == "doki-doki-literature-club":
            # The shared library is linked by three identities: DDLC, Katawa,
            # and a neutral Ren'Py identity for other game installations.
            allowed_ids.add("me.hirameki.kotonoha.katawashoujo")
            allowed_ids.add("me.hirameki.kotonoha.renpy")
            allowed_paths.add("katawa-shoujo")
        if any(metadata_id not in allowed_ids for metadata_id in metadata_ids):
            issues.append(
                f"{slug}: SDL metadata ID is outside the configured bundle "
                f"identities {sorted(allowed_ids)}"
            )

        # SDLSession sets SDL metadata from Runtime::identify when Engine
        # initializes. Matching only the main.cpp metadata is insufficient.
        runtime_files = list((root / "src").glob("*Runtime.cpp"))
        if slug == "example":
            runtime_files.append(root / "src/DemoGame.cpp")
        runtime_ids = []
        for runtime in runtime_files:
            source_text = runtime.read_text(encoding="utf-8")
            runtime_ids.extend(
                re.findall(
                    r'\bRuntime\s*\(\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*"([^"]+)"',
                    source_text, re.S
                )
            )
            runtime_ids.extend(
                re.findall(
                    r'"(me\.hirameki\.kotonoha\.[^"]+)"', source_text
                )
            )
        # Shared runtime libraries may receive an identity through a
        # constructor argument (MEO) or select it from game assets (Katawa).
        # In those cases check the actual wiring instead of requiring a
        # literal package ID inside a per-port Runtime.cpp.
        if not runtime_ids and bundle:
            expected = bundle.group(1)
            if slug == "summer-radish-vacation":
                header = (root / "src/SummerRuntime.hpp").read_text(
                    encoding="utf-8"
                )
                if expected in header and "runtime(assets)" in source:
                    runtime_ids.append(expected)
            elif slug == "meo":
                shared = (ROOT / "games/summer-radish-vacation/src/SummerRuntime.cpp").read_text(
                    encoding="utf-8"
                )
                if (re.search(
                    r'runtime\(assets,\s*"Miss Each Other",\s*"' +
                    re.escape(expected) + r'"\)', source
                ) and 'Runtime(std::move(name), "0.1.0", std::move(appId))' in shared):
                    runtime_ids.append(expected)
            elif slug == "katawa-shoujo":
                shared = (ROOT / "games/doki-doki-literature-club/src/DdlcRuntime.cpp").read_text(
                    encoding="utf-8"
                )
                if (expected in shared and '"Katawa Shoujo"' in shared and "Ddlc::GameRuntime game" in source):
                    runtime_ids.append(expected)
        if not runtime_ids:
            issues.append(f"{slug}: runtime bundle identity was not found")
        for runtime_id in runtime_ids:
            if runtime_id not in allowed_ids:
                issues.append(
                    f"{slug}: runtime identifier {runtime_id} is outside "
                    f"the configured bundle identities {sorted(allowed_ids)}"
                )

        # All path lookups must use the same slug as CMake/Xcode packaging.
        for call, name in re.findall(
            r'\b(gameAssetDirectory|findGameAsset|findGamePath)\s*\(\s*"([^"]+)"',
            source
        ):
            if name not in allowed_paths:
                issues.append(
                    f"{slug}: {call} references '{name}', expected one of "
                    f"{sorted(allowed_paths)}"
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

    # All packaged ports share a real launcher; none may override the
    # explicit --no-launcher choice after CommandLine::parse().
    for target, slug in manifest["games"].items():
        if slug == "example":
            continue
        source = (ROOT / "games" / slug / "src/main.cpp").read_text()
        if "launcher->open()" not in source:
            issues.append(f"{slug}: missing launcher open path")
        if re.search(r"\\b(?:app->)?options\\.launcher\\s*=\\s*true\\b", source):
            issues.append(
                f"{slug}: startup forcibly enables launcher despite --no-launcher"
            )

    shared_cli = (ROOT / "lib/src/core/CommandLine.cpp").read_text()
    for required in ("frames > 0", "engine.enableMCP",
                     "engine.launcher = false"):
        if required not in shared_cli:
            issues.append(f"Shared CLI missing headless launcher policy: {required}")

    # Every CMake game must be discoverable in both human-facing indices.
    for language in ("README.md", "README.pt-BR.md"):
        index = (ROOT / "games" / language).read_text(encoding="utf-8")
        for slug in manifest["games"].values():
            if f"({slug}/README" not in index:
                issues.append(f"{language}: missing port index entry for {slug}")

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
