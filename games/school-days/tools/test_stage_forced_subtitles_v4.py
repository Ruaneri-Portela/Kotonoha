#!/usr/bin/env python3
"""Unit tests for School Days HQ forced_subtitles v4 staging and validation."""

from __future__ import annotations

import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import stage_forced_subtitles_v4 as v4


def build_synthetic_v4_pack(root: Path) -> tuple[Path, dict[str, dict[str, str]], str]:
    """Create a synthetic 198-file pack exercising the exact v4 layout rules."""
    fs = root / "game-assets" / "forced_subtitles"
    fs.mkdir(parents=True)
    hashes: dict[str, dict[str, str]] = {}
    payloads: dict[str, dict[str, bytes]] = {}
    for locale in v4.LOCALES:
        hashes[locale] = {}
        payloads[locale] = {}
        for character in ("Sekai", "Setsuna", "Kotonoha"):
            extra = (
                r"Dialogue: 0,0:01:32.23,0:01:40.65,Sub,,0,0,0,,{\pos(400,325)}A\n"
                r"Dialogue: 0,0:01:40.90,0:01:47.94,Sub,,0,0,0,,{\pos(400,420)}B\n"
                if character == "Setsuna"
                else f"Dialogue: 0,0:00:10.00,0:00:12.00,Sub,,0,0,0,,{locale}-{character}\n"
            )
            content = f"[Script Info]\nTitle: {locale}-{character}-v4\n{extra}".encode("utf-8")
            payloads[locale][character] = content
            hashes[locale][character] = hashlib.sha256(content).hexdigest()

    for node, character in v4.NODE_CHARACTER_MAP.items():
        (fs / node).write_bytes(payloads["en"][character])
    for locale in v4.LOCALES:
        loc_dir = fs / locale
        loc_dir.mkdir()
        for node, character in v4.NODE_CHARACTER_MAP.items():
            (loc_dir / node).write_bytes(payloads[locale][character])

    expected = v4.expected_relative_hashes(hashes)
    tree_sha = v4.compute_tree_sha256(expected)
    return root, hashes, tree_sha


class StageForcedSubtitlesV4Tests(unittest.TestCase):
    def test_contract_counts_and_character_breakdown(self) -> None:
        self.assertEqual(len(v4.LOCALES), 5)
        self.assertEqual(len(v4.NODE_CHARACTER_MAP), 33)
        self.assertEqual(v4.TOTAL_FILES, 198)
        counts = {"Sekai": 0, "Kotonoha": 0, "Setsuna": 0}
        for character in v4.NODE_CHARACTER_MAP.values():
            counts[character] += 1
        self.assertEqual(counts, {"Sekai": 17, "Kotonoha": 14, "Setsuna": 2})
        self.assertEqual(
            v4.compute_tree_sha256(v4.expected_relative_hashes(v4.V4_SOURCE_SHA256)),
            v4.V4_CANONICAL_TREE_SHA256,
        )

    def test_stage_and_verify_replaces_obsolete_files_and_creates_backup(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            src, hashes, tree_sha = build_synthetic_v4_pack(base / "source")
            dest = base / "dist" / "game-assets" / "forced_subtitles"
            dest.mkdir(parents=True)
            (dest / "stale-v3-file.ass").write_text("old-v3", encoding="utf-8")
            (dest / "01-01-00-OP1.ass").write_text("old-v3-op1", encoding="utf-8")
            backup = base / "backup-pre-v4"

            report = v4.stage_directory(
                src,
                dest,
                backup=backup,
                source_sha256=hashes,
                expected_tree_sha256=tree_sha,
            )
            self.assertEqual(report["fileCount"], 198)
            self.assertEqual(report["treeSha256"], tree_sha)
            self.assertFalse((dest / "stale-v3-file.ass").exists())
            self.assertTrue((backup / "stale-v3-file.ass").is_file())
            self.assertEqual((backup / "01-01-00-OP1.ass").read_text(encoding="utf-8"), "old-v3-op1")

    def test_rejects_missing_or_modified_or_extra_files(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            src, hashes, tree_sha = build_synthetic_v4_pack(base / "source")
            fs = src / "game-assets" / "forced_subtitles"

            # 1. Extra obsolete file rejected
            extra = fs / "pt-BR" / "obsolete.ass"
            extra.write_text("stale", encoding="utf-8")
            with self.assertRaises(v4.ForcedSubtitlesError):
                v4.verify_directory(src, source_sha256=hashes, expected_tree_sha256=tree_sha)
            extra.unlink()

            # 2. Tampered file (e.g. old v3 or moved Setsuna pos) rejected
            op2 = fs / "pt-BR" / "01-01-00-OP2.ass"
            original = op2.read_bytes()
            op2.write_bytes(original.replace(b"400,325", b"400,300"))
            with self.assertRaises(v4.ForcedSubtitlesError):
                v4.verify_directory(src, source_sha256=hashes, expected_tree_sha256=tree_sha)
            op2.write_bytes(original)

            # 3. Missing locale file rejected
            missing = fs / "es" / "02-02-2K-OP1.ass"
            saved = missing.read_bytes()
            missing.unlink()
            with self.assertRaises(v4.ForcedSubtitlesError):
                v4.verify_directory(src, source_sha256=hashes, expected_tree_sha256=tree_sha)
            missing.write_bytes(saved)

            # 4. Restored pack passes again
            ok = v4.verify_directory(src, source_sha256=hashes, expected_tree_sha256=tree_sha)
            self.assertEqual(ok["fileCount"], 198)

    def test_rejects_missing_source_without_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(v4.ForcedSubtitlesError):
                v4.stage_directory(Path(tmp) / "does-not-exist", Path(tmp) / "dest")


if __name__ == "__main__":
    unittest.main()
