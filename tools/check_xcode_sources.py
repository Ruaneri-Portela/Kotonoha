#!/usr/bin/env python3
"""Check Xcode navigator groups and macOS/iOS source membership against Git.

Run from any directory: python3 tools/check_xcode_sources.py
No Xcode installation or third-party Python modules are required.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "buildsystems/xcode/Kotonoha.xcodeproj/project.pbxproj"
ID = r"[0-9A-F]{24}"


def section(project: str, name: str) -> str:
    return project.split(f"/* Begin {name} section */", 1)[1].split(
        f"/* End {name} section */", 1
    )[0]


def objects(project: str, name: str) -> dict[str, str]:
    expression = rf"(?ms)^\s*({ID})(?: /\* [^*]+ \*/)? = \{{(.*?)^\s*\}};"
    return dict(re.findall(expression, section(project, name)))


def ids_in(field: str, body: str) -> set[str]:
    match = re.search(rf"\b{field} = \((.*?)\);", body, re.S)
    return set(re.findall(ID, match.group(1))) if match else set()


def main() -> int:
    errors: list[str] = []
    project = PROJECT.read_text(encoding="utf-8")
    groups = objects(project, "PBXGroup")
    file_refs = objects(project, "PBXFileReference")
    build_refs = objects(project, "PBXBuildFile")
    phases = objects(project, "PBXSourcesBuildPhase")
    targets = objects(project, "PBXNativeTarget")

    owners: dict[str, list[str]] = {}
    for group_id, body in groups.items():
        for member in ids_in("children", body):
            owners.setdefault(member, []).append(group_id)
    for file_id in file_refs:
        if len(owners.get(file_id, [])) != 1:
            errors.append(f"File reference {file_id} has {len(owners.get(file_id, []))} navigator parents")
    root = "42F30634C2CFADA428E6AA3C"
    for item in ids_in("children", groups[root]):
        if item in file_refs:
            errors.append(f"Source/reference left in Xcode root: {item}")

    file_paths: dict[str, Path] = {}
    for ref_id, body in file_refs.items():
        path = re.search(r'\bpath = (?:"([^"]+)"|([^;]+));', body)
        if not path or "sourceTree = SOURCE_ROOT" not in body:
            continue
        raw = path.group(1) or path.group(2)
        file_paths[ref_id] = (PROJECT.parent.parent / raw).resolve()

    build_to_ref = {}
    for build_id, body in build_refs.items():
        match = re.search(rf"\bfileRef = ({ID})", body)
        if match:
            build_to_ref[build_id] = match.group(1)
            if match.group(1) not in file_refs:
                errors.append(f"Build file {build_id} has missing file reference")

    def target_sources(name: str) -> set[Path]:
        for body in targets.values():
            if re.search(rf'\bname = "?{re.escape(name)}"?;', body):
                phase_ids = ids_in("buildPhases", body)
                included = set()
                for pid in phase_ids:
                    if pid in phases:
                        included.update(
                            file_paths.get(build_to_ref.get(bid, ""))
                            for bid in ids_in("files", phases[pid])
                        )
                return included
        errors.append(f"Missing Xcode target: {name}")
        return set()

    mac = target_sources("KotonohaEngine-macOS")
    ios = target_sources("KotonohaEngine-iOS")
    tracked = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files", "lib/src"],
        capture_output=True, text=True, check=True,
    ).stdout.splitlines()
    engine_sources = {
        (ROOT / relative).resolve()
        for relative in tracked
        if relative.endswith((".cpp", ".mm"))
    }
    for source in sorted(engine_sources):
        if source not in mac:
            errors.append(f"Engine macOS missing: {source.relative_to(ROOT)}")
        if source not in ios:
            errors.append(f"Engine iOS missing: {source.relative_to(ROOT)}")

    for name, game in (
        ("DokiDoki", "doki-doki-literature-club"),
        ("KatawaShoujo", "katawa-shoujo"),
        ("MEO", "meo"),
        ("PonscripterGame", "ponscripter"),
        ("N2SystemGame", "n2system"),
    ):
        app_main = (ROOT / "games" / game / "src/main.cpp").resolve()
        for platform in ("macOS", "iOS"):
            if app_main not in target_sources(f"{name}-{platform}"):
                errors.append(f"{name}-{platform} does not compile its own main.cpp")

    for platform in ("macOS", "iOS"):
        for name in ("DokiDoki", "KatawaShoujo"):
            target = target_sources(f"{name}-{platform}")
            for rel in (
                "modules/vms/renpy/src/Python.cpp",
                "modules/vms/renpy/src/Vm.cpp",
                "modules/providers/rpa/src/DdlcAssets.cpp",
            ):
                if (ROOT / rel).resolve() not in target:
                    errors.append(f"{name}-{platform} missing shared RenPy source: {rel}")

    if "KotonohaLauncher-macOS" not in project:
        errors.append("Unified launcher Xcode target is missing")
    if "BuildUnifiedLauncher.sh" not in project:
        errors.append("Unified launcher build script is missing")

    if errors:
        for issue in errors:
            print("ERROR:", issue, file=sys.stderr)
        return 1
    print(f"OK: {len(engine_sources)} engine sources in both Apple targets")
    print(f"OK: {len(file_refs)} file references have exactly one group")
    print("OK: DDLC, Katawa, MEO, Ponscripter, N2System and launcher targets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
