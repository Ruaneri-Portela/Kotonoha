#!/usr/bin/env python3
"""Launch each macOS .app bundle briefly and report runtime failures.

This is the final verification step for a build matrix: every bundle is
opened once, its stdout/stderr is captured, and crash signatures are checked.

    python3 tools/SmokeTestApps.py out/macos-universal-vcpkg
    python3 tools/SmokeTestApps.py out/build/macos-amd64-macports/outputs
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

MARKERS = (
    "doesNotRecognizeSelector", "NSInvalidArgumentException",
    "EXC_BAD_ACCESS", "Segmentation fault", "Library not loaded",
    "dyld:", "abort() called", "SIGABRT",
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--seconds", type=float, default=4.0)
    args = parser.parse_args()

    apps = sorted(args.directory.glob("*.app"))
    if not apps:
        print(f"error: no .app bundles in {args.directory}", file=sys.stderr)
        return 1
    failed: list[str] = []
    for app in apps:
        executables = list((app / "Contents/MacOS").iterdir())
        if not executables:
            print(f"FAIL {app.name}: no executable")
            failed.append(app.name)
            continue
        extra: list[str] = []
        if app.name.startswith("SchoolDays"):
            route = (ROOT / "games/school-days/game-assets/"
                     "school-days-hq.ktnroute")
            if route.is_file():
                extra = ["--ktrf", str(route)]
        proc = subprocess.Popen([str(executables[0]), *extra],
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        try:
            time.sleep(args.seconds)
            alive = proc.poll() is None
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill()
        output = proc.stdout.read()
        hits = [marker for marker in MARKERS if marker in output]
        rc = proc.returncode
        # SIGTERM (-15) is our own shutdown; negative codes are crashes.
        crashed = (rc is not None and rc < 0 and rc != -15) or bool(hits)
        status = "FAIL" if crashed else "OK"
        print(f"{status} {app.name}: alive_after={alive} rc={rc} "
              f"signals={','.join(hits) if hits else '-'}")
        if crashed and hits:
            print("  " + "\n  ".join(
                line for line in output.splitlines() if any(h in line for h in hits)
            )[:800])
        if crashed:
            failed.append(app.name)
    print(f"\n{len(apps) - len(failed)}/{len(apps)} apps OK")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
