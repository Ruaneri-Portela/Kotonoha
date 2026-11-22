"""Regression tests for the explicit [pasta] [arquivo] manifest generator."""
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
    def test_web_assets_to_explicit_page_root_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            page = Path(directory)
            assets = page / "game-assets"
            (assets / "nss").mkdir(parents=True)
            (page / "index.html").write_text("test", encoding="utf-8")
            (assets / "nss.npa").write_bytes(b"NPA!")
            (assets / "nss" / "0_boot.nsb").write_bytes(b"abc")
            (assets / "README.txt").write_text("ignored")
            (assets / "preload.json").write_text("ignored")
            (assets / "manifest.json").write_text("{}")
            output = page / "manifest.json"
            completed = subprocess.run(
                [sys.executable, str(SCRIPT), str(assets), str(output)],
                capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            result = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(result["entries"], [
                {"path": "nss.npa", "size": 4},
                {"path": "nss/0_boot.nsb", "size": 3},
            ])
            self.assertEqual(result["files"], ["nss.npa", "nss/0_boot.nsb"])
            self.assertEqual((assets / "manifest.json").read_text(), "{}")

    def test_standalone_game_folder_with_output_one_level_above(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game = root / "SteinsGate"
            (game / "dx").mkdir(parents=True)
            (game / "cg.npa").write_bytes(b"NPA!")
            (game / "nss.npa").write_bytes(b"nss")
            (game / "dx" / "config.bin").write_bytes(b"xy")
            (game / "Droid Sans Fallback.ttf").write_bytes(b"font")
            (game / "preload.json").write_text("ignored")
            output = root / "SteinsGate-manifest.json"
            for _ in range(2):
                completed = subprocess.run(
                    [sys.executable, str(SCRIPT), str(game), str(output)],
                    capture_output=True, text=True)
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertIn("with 4 assets", completed.stdout)
                manifest = json.loads(output.read_text(encoding="utf-8"))
                self.assertEqual(manifest["entries"], [
                    {"path": "Droid Sans Fallback.ttf", "size": 4},
                    {"path": "cg.npa", "size": 4},
                    {"path": "dx/config.bin", "size": 2},
                    {"path": "nss.npa", "size": 3},
                ])
                self.assertEqual(manifest["files"],
                                 [entry["path"] for entry in manifest["entries"]])
                self.assertFalse((game / "manifest.json").exists())
                self.assertFalse((root / "SteinsGate-manifest.json.tmp").exists())

    def test_no_implicit_game_assets_subfolder_detection(self):
        with tempfile.TemporaryDirectory() as directory:
            page = Path(directory) / "site"
            assets = page / "game-assets"
            assets.mkdir(parents=True)
            (page / "index.html").write_text("html", encoding="utf-8")
            (assets / "voice.npa").write_bytes(b"voice")
            output = Path(directory) / "index.json"
            manifest = json.loads(module.generate_manifest(page, output).read_text())
            self.assertEqual(manifest["files"], ["game-assets/voice.npa",
                                                  "index.html"])

    def test_custom_output_inside_asset_folder_is_not_indexed(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory)
            (assets / "voice.npa").write_bytes(b"voice")
            output = assets / "catalog.json"
            for _ in range(2):
                manifest = json.loads(module.generate_manifest(assets, output).read_text())
                self.assertEqual(manifest["files"], ["voice.npa"])
            self.assertFalse((assets / "catalog.json.tmp").exists())

    def test_symlinks_are_not_indexed(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            (game / "voice.npa").write_bytes(b"voice")
            (game / "link.npa").symlink_to(game / "voice.npa")
            (game / "loop").symlink_to(game, target_is_directory=True)
            manifest = json.loads(
                module.generate_manifest(game, game / "manifest.json").read_text())
            self.assertEqual(manifest["files"], ["voice.npa"])

    def test_empty_directory_produces_diagnostic_empty_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory) / "assets"
            assets.mkdir()
            output = module.generate_manifest(assets, Path(directory) / "out" / "manifest.json")
            result = json.loads(output.read_text())
            self.assertEqual(result["entries"], [])

    def test_missing_input_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertRaises(ValueError, module.generate_manifest,
                              Path(directory) / "missing",
                              Path(directory) / "manifest.json")

    def test_output_argument_required(self):
        with tempfile.TemporaryDirectory() as directory:
            completed = subprocess.run(
                [sys.executable, str(SCRIPT), directory],
                capture_output=True, text=True)
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn("arquivo", completed.stderr)

    def test_output_directory_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertRaises(ValueError, module.generate_manifest,
                              Path(directory), Path(directory))


if __name__ == "__main__":
    unittest.main()
