"""Regression tests for the single-argument page-root manifest generator."""
from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent / "generate_manifest.py"
SPEC = importlib.util.spec_from_file_location("kotonoha_manifest", SCRIPT)
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class ManifestGeneratorTests(unittest.TestCase):
    def test_writes_index_next_to_html_and_preserves_relative_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            page = Path(directory)
            (page / "index.html").write_text("test", encoding="utf-8")
            (page / "game-assets" / "nss").mkdir(parents=True)
            (page / "game-assets" / "nss.npa").write_bytes(b"NPA!")
            (page / "game-assets" / "nss" / "0_boot.nsb").write_bytes(b"abc")
            (page / "game-assets" / "README.txt").write_text("ignored")
            (page / "game-assets" / "preload.json").write_text("ignored")
            completed = subprocess.run([sys.executable, str(SCRIPT), str(page)],
                                       capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertFalse((page / "game-assets" / "manifest.json").exists())
            result = json.loads((page / "manifest.json").read_text())
            self.assertEqual(result["entries"], [
                {"path": "nss.npa", "size": 4},
                {"path": "nss/0_boot.nsb", "size": 3},
            ])
            self.assertEqual(result["files"], ["nss.npa", "nss/0_boot.nsb"])

    def test_standalone_game_folder_indexes_archives_in_its_own_root(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            (game / "dx").mkdir()
            (game / "cg.npa").write_bytes(b"NPA!")
            (game / "nss.npa").write_bytes(b"nss")
            (game / "dx" / "config.bin").write_bytes(b"xy")
            (game / "Droid Sans Fallback.ttf").write_bytes(b"font")
            (game / "preload.json").write_text("not an asset")
            (game / "manifest.json").write_text('{"entries": []}')
            for _ in range(2):
                result = subprocess.run([sys.executable, str(SCRIPT), str(game)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("with 4 assets", result.stdout)
                manifest = json.loads((game / "manifest.json").read_text())
                self.assertEqual(manifest["entries"], [
                    {"path": "Droid Sans Fallback.ttf", "size": 4},
                    {"path": "cg.npa", "size": 4},
                    {"path": "dx/config.bin", "size": 2},
                    {"path": "nss.npa", "size": 3},
                ])
                self.assertEqual(manifest["files"],
                                 [entry["path"] for entry in manifest["entries"]])
                self.assertFalse((game / "manifest.json.tmp").exists())

    def test_symlinks_are_not_indexed(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            (game / "voice.npa").write_bytes(b"voice")
            (game / "link.npa").symlink_to(game / "voice.npa")
            (game / "loop").symlink_to(game, target_is_directory=True)
            manifest = json.loads(module.generate_manifest(game).read_text())
            self.assertEqual(manifest["files"], ["voice.npa"])

    def test_empty_directory_produces_diagnostic_empty_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            output = module.generate_manifest(Path(directory))
            result = json.loads(output.read_text())
            self.assertEqual(result["entries"], [])

    def test_missing_page_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertRaises(ValueError, module.generate_manifest,
                              Path(directory) / "missing")


if __name__ == "__main__":
    unittest.main()
