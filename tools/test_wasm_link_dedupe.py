#!/usr/bin/env python3
"""Regression checks for the WASM side-module link normalizer."""
import importlib.util
from pathlib import Path
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location(
    "wasm_link_dedupe", Path(__file__).with_name("wasm_link_dedupe.py")
)
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class LinkDedupeTests(unittest.TestCase):
    def test_absolute_relative_and_l_flag(self):
        with tempfile.TemporaryDirectory(dir=".") as tmp:
            root = Path(tmp).resolve()
            archive = root / "libz.a"
            archive.touch()
            relative = str(archive.relative_to(Path.cwd()))
            tokens = [
                "em++", "-L" + str(root), relative, str(archive),
                "-lz", "-lGL",
            ]
            result = module.normalize(tokens)
            self.assertEqual(result, ["em++", "-L" + str(root), "-lz", "-lGL"])

    def test_repeated_full_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "libSDL3.a"
            path.touch()
            result = module.normalize(["em++", str(path), "-O2", str(path)])
            self.assertEqual(result, ["em++", "-O2", str(path)])

    def test_unrelated_libraries_remain(self):
        self.assertEqual(
            module.normalize(["em++", "input.o", "-lGL", "-lm"]),
            ["em++", "input.o", "-lGL", "-lm"],
        )


if __name__ == "__main__":
    unittest.main()
