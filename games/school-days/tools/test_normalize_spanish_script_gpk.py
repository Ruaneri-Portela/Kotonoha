"""Synthetic-only verification of fail-closed Spanish ORS normalization."""

from __future__ import annotations

from collections import Counter
from pathlib import Path
import shutil
import tempfile
import unittest

import normalize_spanish_script_gpk as mod
from build_lxh_locale_gpks import GpkReader, LocaleGpkError


class SpanishNormalizationTests(unittest.TestCase):
    def setUp(self):
        mod.STATE_ROOT.mkdir(parents=True, exist_ok=True)
        self.folder = Path(tempfile.mkdtemp(
            prefix="synthetic-es-", dir=mod.STATE_ROOT))

    def tearDown(self):
        shutil.rmtree(self.folder)

    def test_native_ordinal_includes_unknown_and_sorts_by_tick(self):
        source = (
            b"[SkipFRAME]=00:00:00;\n"
            b"[PrintText]=00:00:03\tSekai\tHello\t00:00:07;\n"
            b"[UnknownCommand]=survive;\n"
            b"[PlaySe]=00:00:01\t1\tmedia\t00:00:05;\n"
            b"[PrintText]=00:00:01\tMakoto\tName\t00:00:06;\n"
            b"[SetSELECT]=00:00:08\tStay\tLeave\t00:00:20;\n"
            b"[Next]=00:00:21;\n"
        )
        document = mod.NativeOrs(source)
        self.assertEqual(
            [e.kind for e in document.events],
            ["SkipFRAME", "UNKNOWN", "PlaySe", "PrintText",
             "PrintText", "SetSELECT", "Next"])
        self.assertEqual(
            [(ordinal, slot) for ordinal, slot, *_ in document.printed_slots()],
            [(3, 0), (4, 0), (5, 0), (5, 1)])
        dictionary = {"7": {
            "@ors:3:0": "Nombre",
            "@ors:4:0": "Hola",
            "@ors:5:0": "Quedarme",
            "@ors:5:1": "Salir",
        }}
        changed = mod.translate_scene(source, 7, dictionary, {}, Counter())
        self.assertIn(b"\tNombre\t", changed)
        self.assertIn(b"\tHola\t", changed)
        self.assertIn(b"\tQuedarme\tSalir\t", changed)
        self.assertNotIn(b"\tStay\t", changed)
        self.assertNotIn(b"\tLeave\t", changed)
        self.assertIn(b"[UnknownCommand]=survive;", changed)
        self.assertEqual(
            [(e.kind, e.start, e.end) for e in mod.NativeOrs(changed).events],
            [(e.kind, e.start, e.end) for e in document.events])

    def test_no_english_fallback_and_reviewed_identity(self):
        source = (
            b"[PrintText]=00:00:02\tMakoto\tMakoto\t00:00:03;\n"
            b"[PrintText]=00:00:04\tMakoto\tHello\t00:00:05;\n"
        )
        with self.assertRaisesRegex(LocaleGpkError, "Unreviewed"):
            mod.translate_scene(source, 2, {"2": {
                "Makoto": "Makoto", "Hello": "Hola",
            }}, {}, Counter())
        with self.assertRaisesRegex(LocaleGpkError, "Missing/blank"):
            mod.translate_scene(source, 2, {"2": {
                "Makoto": "Makoto",
            }}, {"2": {"Makoto": "proper_name"}}, Counter())
        checked = mod.translate_scene(source, 2, {"2": {
            "Makoto": "Makoto", "Hello": "Hola",
        }}, {"2": {"Makoto": "proper_name"}}, Counter())
        self.assertIn(b"\tMakoto\t", checked)
        self.assertIn(b"\tHola\t", checked)
        self.assertNotIn(b"\tHello\t", checked)

    def test_physical_newline_is_escaped_to_one_native_line(self):
        source = b"[PrintText]=00:00:02\tSekai\tHello\t00:00:05;\r\n"
        stats = Counter()
        changed = mod.translate_scene(
            source, 5, {"5": {"Hello": "Hola\notra vez"}}, {}, stats)
        self.assertEqual(stats["escapedNewlines"], 1)
        self.assertIn(b"Hola\\notra vez", changed)
        self.assertNotIn(b"Hola\notra vez", changed)
        self.assertEqual(len(mod.NativeOrs(changed).events), 1)

    def test_out_of_range_ret_tick_is_preserved(self):
        source = b"[PrintText]=01:80:27\tSekai\tOne\t01:81:28;\n"
        converted = mod.translate_scene(
            source, 3, {"3": {"One": "Uno"}}, {}, Counter())
        self.assertIn(b"01:80:27", converted)
        self.assertIn(b"01:81:28", converted)
        self.assertEqual(
            [(e.start, e.end) for e in mod.NativeOrs(source).events],
            [(e.start, e.end) for e in mod.NativeOrs(converted).events])

    def test_write_1859_canonical_synthetic_members_private_only(self):
        test_files = {
            ".DS_STORE": b"example1",
            "ENGLISH/.DS_STORE": b"example2",
        }
        for i in range(1857):
            test_files[f"ENGLISH/00/test-{i:04}.ENG.ORS"] = (
                f"[Next]=00:00:{i % 24:02};\n").encode("utf-8")
        out = self.folder / "Script - Spanish.GPK"
        report = mod.write_full_archive(out, test_files)
        self.assertEqual(len(report["sha256"]), 64)
        reader = GpkReader(out)
        self.assertEqual(len(reader.entries), 1859)
        self.assertEqual(reader.read_ors("ENGLISH/00/test-1856.ENG.ORS"),
                         test_files["ENGLISH/00/test-1856.ENG.ORS"])
        self.assertEqual(
            reader.original_names["english/00/test-1856.eng.ors"],
            "ENGLISH/00/test-1856.ENG.ORS")
        self.assertEqual(reader.read_ors(".DS_STORE"), b"example1")
        with self.assertRaises(FileExistsError):
            mod.write_full_archive(out, test_files)
        with self.assertRaises(LocaleGpkError):
            mod.write_full_archive(self.folder / "outside.GPK", {".DS_STORE": b"a"})


if __name__ == "__main__":
    unittest.main()
