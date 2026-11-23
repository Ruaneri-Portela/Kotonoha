#!/usr/bin/env python3
"""Stage and verify the official School Days HQ forced_subtitles v4 pack.

This utility enforces the pinned v4 forced_subtitles contract (198 ASS files:
33 nodes x 5 locales + 33 legacy root EN files) for private Windows QA,
Android SAF external folders, and v1.00+ distributions.

Forced subtitles remain strictly optional for running base School Days HQ at
runtime; this validator applies exclusively to official QA/distribution staging.
Never embed or commit proprietary/private ASS files into the public Git repo.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

VERSION = "v4"
ENV_SOURCE_VAR = "KOTONOHA_FORCED_SUBTITLES_V4_DIR"
LOCALES = ("en", "es", "pt-BR", "ru", "zh-CN")
LEGACY_ROOT_LOCALE = "en"
NODES_PER_LOCALE = 33
TOTAL_FILES = NODES_PER_LOCALE * (len(LOCALES) + 1)  # 198

NODE_CHARACTER_MAP: dict[str, str] = {
    "01-01-00-OP1.ass": "Sekai",
    "01-01-00-OP2.ass": "Setsuna",
    "02-02-2K-OP1.ass": "Kotonoha",
    "02-02-2S-OP1.ass": "Sekai",
    "03-03-KA-OP1.ass": "Setsuna",
    "03-03-KB-OP1.ass": "Kotonoha",
    "03-03-SA-OP1.ass": "Sekai",
    "03-03-SB-OP1.ass": "Sekai",
    "04-04-KA-OP1.ass": "Kotonoha",
    "04-04-KB-OP1.ass": "Kotonoha",
    "04-04-KC-OP1.ass": "Kotonoha",
    "04-04-KE-OP1.ass": "Kotonoha",
    "04-04-SA-OP1.ass": "Sekai",
    "04-04-SB-OP1.ass": "Sekai",
    "04-04-SC-OP1.ass": "Sekai",
    "04-04-SE-OP1.ass": "Sekai",
    "04-04-SF-OP1.ass": "Sekai",
    "04-04-SG-OP1.ass": "Sekai",
    "05-05-5H-OP1.ass": "Kotonoha",
    "05-05-5O-OP1.ass": "Kotonoha",
    "05-05-KA-OP1.ass": "Kotonoha",
    "05-05-KB-OP1.ass": "Kotonoha",
    "05-05-KC-OP1.ass": "Kotonoha",
    "05-05-KF-OP1.ass": "Kotonoha",
    "05-05-KG-OP1.ass": "Kotonoha",
    "05-05-KI-OP1.ass": "Kotonoha",
    "05-05-SA-OP1.ass": "Sekai",
    "05-05-SB-OP1.ass": "Sekai",
    "05-05-SC-OP1.ass": "Sekai",
    "05-05-SD-OP1.ass": "Sekai",
    "05-05-SE-OP1.ass": "Sekai",
    "05-05-SG-OP1.ass": "Sekai",
    "05-05-SH-OP1.ass": "Sekai",
}

V4_SOURCE_SHA256: dict[str, dict[str, str]] = {
    "en": {
        "Sekai": "743d51522c292cad2cc4e065f9c99d3ff9f47d3fe2db0769880a6c3a40f528b0",
        "Setsuna": "fc7eb6763260493af8a5e9fa9c53592c3ee62f3bead0eaf0d0d37b2b98d12a47",
        "Kotonoha": "d60bf4a25849e2d8a6c2ea790e12cd41fbe21a098c1d1c91229e3442f1b8625f",
    },
    "es": {
        "Sekai": "bf5a5a1db09ec92c2a693b3ae144407634466121aacf58109065689b8b5d24e4",
        "Setsuna": "1f4766c1f0899256049c3642369650076147a95d5db66fa15ce8183968108c74",
        "Kotonoha": "3b43f01998176eb50cd3144e0535cf9bfb5806bf0f602598095a380205891e6f",
    },
    "pt-BR": {
        "Sekai": "51c8d0c95b9e09a808d0cb2dd15bb6af4c9c864d9a0c68cfb436fe200d3b6962",
        "Setsuna": "d7ca190a98f93d938e521f276b52693d8023e22a08aac9cb6d994717dee89ff4",
        "Kotonoha": "e28cd374497a5870d867dc5e1ca15979b964c89582f872712bc35fe6a76a7f8a",
    },
    "ru": {
        "Sekai": "81a3136d839cb8bdf6603558d8647944d13cbba5c2d614e201ac7179926ce00f",
        "Setsuna": "65c4d1167f653742108f6b926f9ef301327ca2984610ce3060c7deb7d71003e9",
        "Kotonoha": "7bc47bea5497e14aae333786d431cebc498c902b2abe41b8c192089e873f42da",
    },
    "zh-CN": {
        "Sekai": "0672f4b4945cf423c5ee01df27ed6381f3624c64c55fa0cde23549081929a172",
        "Setsuna": "1b0ac20ed8fd13db91ea5475343b75393bcf574e3cdc39c237ec641209310b8d",
        "Kotonoha": "f20793c206d3ca29b08562908cdecbba6ec294b2bacefc89b923e0a13df1a369",
    },
}

V4_CANONICAL_TREE_SHA256 = (
    "064172a520b7aae1eabac0a2a078e1b866c821a123c7fe046ac8d7423462a818"
)
REQUIRED_SETSUNA_POSITIONS = (r"\pos(400,325)", r"\pos(400,420)")


class ForcedSubtitlesError(RuntimeError):
    """Raised when a forced_subtitles v4 source or destination fails checks."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().lower()


def resolve_forced_subtitles_dir(path: Path) -> Path:
    """Resolve either a package root or a direct forced_subtitles directory."""
    if not path.exists() or not path.is_dir():
        raise ForcedSubtitlesError(
            f"forced_subtitles {VERSION} source directory not found: {path}. "
            f"Pass --source or set {ENV_SOURCE_VAR} to the approved v4 pack "
            "(never fall back to older v1/v2/v3 subtitles)."
        )
    nested = path / "game-assets" / "forced_subtitles"
    if nested.is_dir():
        return nested
    direct = path / "forced_subtitles"
    if direct.is_dir():
        return direct
    return path


def expected_relative_hashes(
    source_sha256: dict[str, dict[str, str]] = V4_SOURCE_SHA256,
) -> dict[str, str]:
    expected: dict[str, str] = {}
    for node, character in NODE_CHARACTER_MAP.items():
        expected[node] = source_sha256[LEGACY_ROOT_LOCALE][character].lower()
    for locale in LOCALES:
        for node, character in NODE_CHARACTER_MAP.items():
            expected[f"{locale}/{node}"] = source_sha256[locale][character].lower()
    return expected


def compute_tree_sha256(file_hashes: dict[str, str]) -> str:
    lines = [f"{file_hashes[rel]}  {rel}" for rel in sorted(file_hashes)]
    return hashlib.sha256(("\n".join(lines) + "\n").encode("utf-8")).hexdigest()


def verify_directory(
    root: Path,
    *,
    source_sha256: dict[str, dict[str, str]] = V4_SOURCE_SHA256,
    expected_tree_sha256: str | None = V4_CANONICAL_TREE_SHA256,
    check_setsuna_positions: bool = True,
) -> dict[str, object]:
    """Validate a forced_subtitles tree against the pinned v4 specification."""
    fs_dir = resolve_forced_subtitles_dir(root)
    expected = expected_relative_hashes(source_sha256)

    actual_files = {
        p.relative_to(fs_dir).as_posix(): p
        for p in fs_dir.rglob("*")
        if p.is_file()
    }
    missing = sorted(set(expected) - set(actual_files))
    extra = sorted(set(actual_files) - set(expected))
    if missing:
        raise ForcedSubtitlesError(
            f"Missing {len(missing)} required forced_subtitles {VERSION} "
            f"file(s) in {fs_dir} (first: {missing[:5]})"
        )
    if extra:
        raise ForcedSubtitlesError(
            f"Found {len(extra)} unexpected/obsolete file(s) in {fs_dir} "
            f"(first: {extra[:5]}). Refuse mixing old and v4 subtitle files."
        )

    actual_hashes: dict[str, str] = {}
    mismatches: list[str] = []
    for rel in sorted(expected):
        h = sha256_file(actual_files[rel])
        actual_hashes[rel] = h
        if h != expected[rel]:
            mismatches.append(f"{rel} (got {h[:12]}..., expected {expected[rel][:12]}...)")
    if mismatches:
        raise ForcedSubtitlesError(
            f"SHA-256 mismatch in {len(mismatches)} forced_subtitles {VERSION} "
            f"file(s) under {fs_dir} (first: {mismatches[:5]}). "
            "Older v1/v2/v3 or edited files are rejected."
        )

    # Confirm all 5 locales remain distinct (no cross-locale clobbering).
    sekai_by_locale = {actual_hashes[f"{loc}/01-01-00-OP1.ass"] for loc in LOCALES}
    if len(sekai_by_locale) != len(LOCALES):
        raise ForcedSubtitlesError(
            f"Locale separation failed in {fs_dir}: duplicate OP1 digests across locales"
        )

    if check_setsuna_positions:
        setsuna_nodes = [n for n, ch in NODE_CHARACTER_MAP.items() if ch == "Setsuna"]
        for loc in LOCALES:
            for node in setsuna_nodes:
                text = actual_files[f"{loc}/{node}"].read_text(
                    encoding="utf-8-sig", errors="replace"
                )
                for marker in REQUIRED_SETSUNA_POSITIONS:
                    if marker not in text:
                        raise ForcedSubtitlesError(
                            f"Approved Setsuna position {marker} missing in {loc}/{node}"
                        )

    tree_sha = compute_tree_sha256(actual_hashes)
    if expected_tree_sha256 is not None and tree_sha != expected_tree_sha256.lower():
        raise ForcedSubtitlesError(
            f"Canonical tree SHA-256 mismatch for {fs_dir}: "
            f"got {tree_sha}, expected {expected_tree_sha256.lower()}"
        )

    return {
        "version": VERSION,
        "path": str(fs_dir),
        "fileCount": len(actual_hashes),
        "perLocale": {loc: NODES_PER_LOCALE for loc in LOCALES},
        "legacyRootCount": NODES_PER_LOCALE,
        "treeSha256": tree_sha,
    }


def stage_directory(
    source: Path,
    destination: Path,
    *,
    backup: Path | None = None,
    source_sha256: dict[str, dict[str, str]] = V4_SOURCE_SHA256,
    expected_tree_sha256: str | None = V4_CANONICAL_TREE_SHA256,
    check_setsuna_positions: bool = True,
) -> dict[str, object]:
    """Verify source v4, optionally back up destination, and stage cleanly."""
    src_fs = resolve_forced_subtitles_dir(source)
    verify_directory(
        src_fs,
        source_sha256=source_sha256,
        expected_tree_sha256=expected_tree_sha256,
        check_setsuna_positions=check_setsuna_positions,
    )

    dest = destination.resolve()
    if src_fs.resolve() == dest:
        raise ForcedSubtitlesError("Source and destination must be distinct paths")

    if backup is not None and dest.exists():
        backup_dir = backup.resolve()
        if backup_dir.exists():
            raise ForcedSubtitlesError(f"Backup path already exists: {backup_dir}")
        backup_dir.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(dest, backup_dir)

    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp_dir = Path(tempfile.mkdtemp(prefix=".forced_subtitles_v4_", dir=dest.parent))
    try:
        staged = tmp_dir / "forced_subtitles"
        shutil.copytree(src_fs, staged)
        verify_directory(
            staged,
            source_sha256=source_sha256,
            expected_tree_sha256=expected_tree_sha256,
            check_setsuna_positions=check_setsuna_positions,
        )
        old_swap = None
        if dest.exists():
            old_swap = tmp_dir / "old_forced_subtitles"
            dest.rename(old_swap)
        staged.rename(dest)
        if old_swap is not None and old_swap.exists():
            shutil.rmtree(old_swap)
    finally:
        if tmp_dir.exists():
            shutil.rmtree(tmp_dir, ignore_errors=True)

    return verify_directory(
        dest,
        source_sha256=source_sha256,
        expected_tree_sha256=expected_tree_sha256,
        check_setsuna_positions=check_setsuna_positions,
    )


def resolve_source_arg(cli_source: str | None) -> Path:
    raw = cli_source or os.environ.get(ENV_SOURCE_VAR, "").strip()
    if not raw:
        raise ForcedSubtitlesError(
            f"No forced_subtitles {VERSION} source specified. "
            f"Pass --source <v4-dir> or set {ENV_SOURCE_VAR}."
        )
    return Path(raw)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Stage and validate official School Days HQ forced_subtitles v4 "
            "(198 ASS files across 5 locales + EN legacy root)."
        )
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_verify = sub.add_parser("verify", help="Verify an existing forced_subtitles tree")
    p_verify.add_argument("target", type=Path, help="Path to forced_subtitles or package root")
    p_verify.add_argument("--json", action="store_true", help="Emit JSON summary")

    p_stage = sub.add_parser("stage", help="Cleanly stage forced_subtitles v4 into a target")
    p_stage.add_argument(
        "--source",
        default=None,
        help=f"Approved v4 directory (or set {ENV_SOURCE_VAR})",
    )
    p_stage.add_argument("--dest", type=Path, required=True, help="Target forced_subtitles dir")
    p_stage.add_argument("--backup", type=Path, default=None, help="Optional backup dir")
    p_stage.add_argument("--json", action="store_true", help="Emit JSON summary")

    args = parser.parse_args(argv)
    try:
        if args.command == "verify":
            report = verify_directory(args.target)
        else:
            src = resolve_source_arg(args.source)
            report = stage_directory(src, args.dest, backup=args.backup)
    except ForcedSubtitlesError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(
            f"[forced_subtitles {report['version']}] OK: {report['fileCount']} files "
            f"(5 locales x 33 + 33 root EN), tree SHA-256={report['treeSha256']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
