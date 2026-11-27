#!/usr/bin/env python3
"""Build the Kotonoha iOS scheme variants with xcodebuild.

    python3 tools/BuildIOS.py --sdk iphonesimulator
    python3 tools/BuildIOS.py --sdk iphoneos
    python3 tools/BuildIOS.py --sdk all

Every iOS scheme from the checked-in Xcode project is built once per SDK,
using CODE_SIGNING_ALLOWED=NO (no signing credentials needed to compile).
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "buildsystems/xcode/Kotonoha.xcodeproj"
BUILD_ROOT = ROOT / "out/xcode-build"


def say(message: str) -> None:
    print(f"[Kotonoha/BuildIOS] {message}", flush=True)


def ios_schemes() -> list[str]:
    result = subprocess.run(["xcodebuild", "-list", "-project", str(PROJECT)],
                            cwd=ROOT, capture_output=True, text=True, check=True)
    in_schemes = False
    found: list[str] = []
    for line in result.stdout.splitlines():
        if line.strip() == "Schemes:":
            in_schemes = True
            continue
        if in_schemes:
            match = re.match(r'^\s+(.+ \(iOS\))\s*$', line)
            if match:
                found.append(match.group(1))
    return found


def build(scheme: str, sdk: str, configuration: str) -> bool:
    destination = ("generic/platform=iOS Simulator" if sdk == "iphonesimulator"
                   else "generic/platform=iOS")
    derived = BUILD_ROOT / f"{scheme.replace(' ', '_')}-{sdk}-{configuration}"
    cmd = ["xcodebuild", "-project", str(PROJECT), "-scheme", scheme,
           "-configuration", configuration, "-sdk", sdk,
           "-destination", destination, "CODE_SIGNING_ALLOWED=NO",
           "-derivedDataPath", str(derived), "build"]
    say(f"{scheme} [{sdk}]: {' '.join(cmd)}")
    import os
    env = dict(os.environ)
    # Aggregate iOS schemes (e.g. the launcher) do not export PLATFORM_NAME
    # to their pre-action; make the dependency selector use the SDK we build.
    env["PLATFORM_NAME"] = sdk
    result = subprocess.run(cmd, cwd=ROOT, env=env)
    return result.returncode == 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", choices=("iphoneos", "iphonesimulator", "all"),
                        default="iphonesimulator")
    parser.add_argument("--configuration", default="Debug")
    parser.add_argument("--only", help="build only this scheme")
    args = parser.parse_args()

    schemes = [args.only] if args.only else ios_schemes()
    if not schemes:
        print("error: no iOS schemes found", file=sys.stderr)
        return 1
    sdks = ["iphoneos", "iphonesimulator"] if args.sdk == "all" else [args.sdk]
    say(f"{len(schemes)} schemes x {len(sdks)} SDKs")
    failed: list[str] = []
    for sdk in sdks:
        for scheme in schemes:
            if not build(scheme, sdk, args.configuration):
                failed.append(f"{scheme}/{sdk}")
    say(f"OK: {len(schemes) * len(sdks) - len(failed)}/"
        f"{len(schemes) * len(sdks)} builds")
    if failed:
        for item in failed:
            print(f"  FAILED {item}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
