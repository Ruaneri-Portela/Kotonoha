#!/usr/bin/env python3
"""Standard Python driver for Kotonoha's native Xcode builds.

It prepares dependencies through tools/PrepareApple.py (vcpkg per
architecture/platform plus SDL frameworks per configuration) and then runs
xcodebuild for the requested scheme. This is the same path used by the Xcode
UI, including the scheme pre-actions.

    python3 tools/BuildApple.py --configuration Debug --arch x86_64
    python3 tools/BuildApple.py --configuration Release --arch arm64
    python3 tools/BuildApple.py --configuration Release --arch universal
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "buildsystems/xcode/Kotonoha.xcodeproj"
PREPARE = ROOT / "tools/PrepareApple.py"
BUILD_ROOT = ROOT / "out/xcode-build"

MACOS_ARCHS = ("arm64", "x86_64")


def say(message: str) -> None:
    print(f"[Kotonoha/BuildApple] {message}", flush=True)


def prepare(platform_name: str, configuration: str, archs: list[str]) -> None:
    env = dict(os.environ)
    env.update(PLATFORM_NAME=platform_name, CONFIGURATION=configuration,
               ARCHS=" ".join(archs))
    subprocess.run([sys.executable, str(PREPARE), "--xcode-pre-action"],
                   check=True, cwd=ROOT, env=env)


def binary_archs(binary: Path) -> set[str]:
    try:
        result = subprocess.run(["xcrun", "lipo", "-archs", str(binary)],
                                check=True, capture_output=True, text=True)
        return set(result.stdout.split())
    except (OSError, subprocess.CalledProcessError):
        return set()


def build(scheme: str, configuration: str, archs: list[str]) -> Path:
    derived = BUILD_ROOT / f"{scheme.replace(' ', '_')}-{configuration}-{'-'.join(archs)}"
    derived.mkdir(parents=True, exist_ok=True)
    cmd = ["xcodebuild", "-project", str(PROJECT), "-scheme", scheme,
           "-configuration", configuration, "-sdk", "macosx",
           "ARCHS=" + " ".join(archs), "ONLY_ACTIVE_ARCH=NO",
           "CODE_SIGNING_ALLOWED=NO", "-derivedDataPath", str(derived), "build"]
    # The arm64 root holds universal SDL/vcpkg dylibs, so it can serve both
    # slices of a universal build; use the amd64 root for x86_64-only builds.
    folder = ("macos-arm64-vcpkg" if "arm64" in archs
              else "macos-amd64-vcpkg")
    root = ROOT / "out/build" / folder / "vcpkg_installed"
    cmd.extend((f"KOTONOHA_VCPKG_PARENT={root}",
                f"KOTONOHA_VCPKG_TRIPLET={folder}",
                f"KOTONOHA_VCPKG_INCLUDE={root}/{folder}/include",
                f"KOTONOHA_VCPKG_LIB={root}/{folder}/lib"))
    say(" ".join(cmd))
    subprocess.run(cmd, check=True, cwd=ROOT)
    products = derived / "Build/Products" / configuration
    # Product names do not always match the scheme (e.g. the KotonohaLauncher
    # scheme produces Kotonoha.app), so discover the bundle instead of guessing.
    apps = sorted(products.glob("*.app"))
    if not apps:
        raise RuntimeError(f"No .app found in {products}")
    return apps[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scheme", default="KotonohaExample (macOS)")
    parser.add_argument("--configuration", choices=("Debug", "Release"),
                        default="Debug")
    parser.add_argument("--arch", choices=("arm64", "x86_64", "universal"),
                        default="universal")
    parser.add_argument("--only-prepare", action="store_true",
                        help="Prepare dependencies without building")
    args = parser.parse_args()
    archs = list(MACOS_ARCHS) if args.arch == "universal" else [args.arch]
    try:
        prepare("macosx", args.configuration, archs)
        if args.only_prepare:
            return 0
        app = build(args.scheme, args.configuration, archs)
        executables = list((app / "Contents/MacOS").iterdir())
        if not executables:
            raise RuntimeError(f"No executable in {app}")
        executable = executables[0]
        actual = binary_archs(executable)
        if not actual.issuperset(archs):
            raise RuntimeError(
                f"{executable} has {sorted(actual)}, expected {archs}")
        say(f"OK: {app} ({', '.join(sorted(actual))})")
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"error: [Kotonoha/BuildApple] {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
