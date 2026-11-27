#!/usr/bin/env python3
"""Keep every Xcode scheme pre-action on the same Python entry point.

The checked-in schemes used to embed a slightly different shell snippet per
platform. This tool rewrites those snippets to a single Python invocation of
tools/PrepareApple.py --xcode-pre-action, which discovers platform, SDK,
architecture and configuration from the Xcode build environment.

    python3 tools/StandardizeXcodePreActions.py
    python3 tools/StandardizeXcodePreActions.py --check
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCHEMES = ROOT / "buildsystems/xcode/Kotonoha.xcodeproj/xcshareddata/xcschemes"
SCRIPT = ("/usr/bin/env python3 &quot;${SRCROOT}/../../tools/PrepareApple.py&quot; "
          "--xcode-pre-action")
ACTION = re.compile(
    r'(<ActionContent\b[^>]*?\bscriptText\s*=\s*")([^"]*)(")',
    re.DOTALL,
)


def standardize(path: Path, *, check_only: bool) -> bool:
    text = path.read_text(encoding="utf-8")
    if "PrepareApple.py" not in text:
        return False
    updated, count = ACTION.subn(
        lambda match: match.group(1) + SCRIPT + match.group(3), text
    )
    if count == 0:
        raise RuntimeError(f"No scriptText found in {path}")
    if updated == text:
        return False
    if not check_only:
        with path.open("w", encoding="utf-8", newline="") as stream:
            stream.write(updated)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="Only report schemes that need standardization")
    args = parser.parse_args()
    changed: list[Path] = []
    for path in sorted(SCHEMES.glob("*.xcscheme")):
        try:
            if standardize(path, check_only=args.check):
                changed.append(path)
        except (OSError, RuntimeError) as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1
    verb = "need standardization" if args.check else "standardized"
    print(f"[Kotonoha] {len(changed)} Xcode schemes {verb}")
    for path in changed:
        print(f"  {path.relative_to(ROOT)}")
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    sys.exit(main())
