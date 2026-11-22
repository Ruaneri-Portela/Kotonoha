#!/usr/bin/env python3
"""Fast, dependency-free repository and redistribution hygiene guard.

It checks tracked files only. It does not replace an SBOM, legal review,
commercial-game tests or license notices for runtime libraries.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = (
    "LICENSE",
    "THIRD_PARTY_NOTICES.md",
    "lib/LICENSE",
    "assets/Anime4K/LICENSE-Anime4K.txt",
    "assets/Anime4K/SOURCE.txt",
    "assets/ArtCNN/LICENSE-ArtCNN.txt",
    "assets/ArtCNN/SOURCE.txt",
    "modules/providers/amagami/third_party/open-amagami-LICENSE.txt",
    "docs/LICENSING.md",
    "docs/LICENSING.pt-BR.md",
)
# These entries are small, author-created metadata/demo material, not retail
# game archives. Additions to this allowlist require a provenance review.
ALLOWED_GAME_ASSETS = {
    "games/example/game-assets/prologue.timeline",
    "games/example/game-assets/translation.json",
    "games/school-days/game-assets/school-days-hq.ktnroute",
    "games/school-days/game-assets/styles.ass",
}
METADATA_FILENAMES = {".gitignore", ".gitkeep", "README.md", "README.txt"}


def main() -> int:
    try:
        raw = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f"error: cannot list tracked files: {exc}", file=sys.stderr)
        return 2
    tracked = {path.decode("utf-8", "surrogateescape")
               for path in raw.split(b"\0") if path}
    errors: list[str] = []
    warnings: list[str] = []

    for path in REQUIRED:
        if path not in tracked or not (ROOT / path).is_file():
            errors.append(f"missing required tracked notice: {path}")

    license_file = ROOT / "LICENSE"
    if license_file.exists() and "MIT License" not in license_file.read_text(
            encoding="utf-8"):
        errors.append("root LICENSE does not contain the expected MIT notice")

    for path in sorted(tracked):
        parts = path.split("/")
        if len(parts) >= 4 and parts[0] == "games" and parts[2] == "game-assets":
            if (path not in ALLOWED_GAME_ASSETS
                    and parts[-1] not in METADATA_FILENAMES):
                errors.append(f"review tracked game asset before distribution: {path}")
        if path.lower().endswith((".pem", ".p8", ".p12",
                                  ".mobileprovision", ".keystore", ".jks")):
            errors.append(f"tracked signing material: {path}")

    cmake = ROOT / "buildsystems/cmake/packaging/KotonohaCPack.cmake"
    if cmake.is_file():
        cmake_source = cmake.read_text(encoding="utf-8")
        if cmake_source.count('PATTERN "game-assets" EXCLUDE') < 2:
            errors.append("both macOS bundle and other CPack paths must exclude game-assets")
        if "THIRD_PARTY_NOTICES.md" not in cmake_source:
            errors.append("CPack must install third-party notices")

    ignore = ROOT / ".gitignore"
    if ignore.is_file():
        source = ignore.read_text(encoding="utf-8-sig")
        if "debug/**![Gg]ames" in source:
            errors.append("concatenated .gitignore debug patterns")
        for pattern in ("*.pem", "*.p8", "*.p12", "*.mobileprovision"):
            if pattern not in source.splitlines():
                errors.append(f"missing ignore rule: {pattern}")

    for path in ("assets/SPLASH.PNG", "assets/icon.icns",
                 "assets/icon.ico", "lib/tests/fixtures/SvgTestFont.ttf"):
        if path in tracked:
            warnings.append(
                f"confirm provenance/redistribution rights separately: {path}")

    print(f"Checked {len(tracked)} tracked paths; "
          f"{len(errors)} error(s), {len(warnings)} provenance reminder(s).")
    for warning in warnings:
        print(f"NOTICE: {warning}")
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
