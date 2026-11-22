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
