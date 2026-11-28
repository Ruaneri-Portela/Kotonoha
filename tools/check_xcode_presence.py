#!/usr/bin/env python3
"""Check that each consuming Xcode target owns exactly one RPC transport."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "buildsystems/xcode/Kotonoha.xcodeproj/project.pbxproj"

def check(project):
    refs = dict(re.findall(
        r"^\s*([A-F0-9]{24}) /\* (RichPresence\.cpp|DiscordUnix\.cpp|NullPresence\.cpp) \*/ = \{isa = PBXFileReference;",
        project, re.M))
    files = dict(re.findall(
        r"^\s*([A-F0-9]{24}) /\* [^\n]*? \*/ = \{isa = PBXBuildFile; fileRef = ([A-F0-9]{24}) ",
        project, re.M))
    phases = dict(re.findall(
        r"^\t\t([A-F0-9]{24}) /\* Sources \*/ = \{\n([\s\S]*?)^\t\t\};",
        project, re.M))
    targets = dict(re.findall(
        r"^\t\t[A-F0-9]{24} /\* ([^\n]+) \*/ = \{\n\t\t\tisa = PBXNativeTarget;([\s\S]*?)^\t\t\};",
        project, re.M))
    used = set()
    for prefix in ("SchoolDays", "KotonohaLauncher", "KotonohaLauncherCore"):
        for platform in ("macOS", "iOS"):
            name = f"{prefix}-{platform}"
            target = targets.get(name, "")
            match = re.search(r"([A-F0-9]{24}) /\* Sources \*/,", target)
            assert match, f"{name}: missing source phase"
            phase = phases[match.group(1)]
            source_ids = re.findall(r"^\s*([A-F0-9]{24}) /\* [^\n]*? \*/,", phase, re.M)
            presence = [(source, refs[files[source]]) for source in source_ids
                        if source in files and files[source] in refs]
            expected = ["RichPresence.cpp", "DiscordUnix.cpp" if platform == "macOS" else "NullPresence.cpp"]
            assert sorted(filename for _, filename in presence) == sorted(expected), (
                f"{name}: expected {expected}, got {presence}")
            for source, filename in presence:
                assert source not in used, f"{name}: shared build-file ID for {filename}"
                used.add(source)
            print(f"[Xcode RPC] {name}: {', '.join(expected)}")

if __name__ == "__main__":
    check(PROJECT.read_text(encoding="utf-8"))
