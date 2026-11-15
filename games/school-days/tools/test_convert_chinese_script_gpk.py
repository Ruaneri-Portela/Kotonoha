"""Synthetic-only fail-closed tests for the Chinese GPK conversion auditor.

All fixtures and outputs are private, generated from invented text/events under
tools/qa-lab/state, and deleted after each test. No retail pack is modified.
"""

from __future__ import annotations

from pathlib import Path
import shutil
import struct
import tempfile
import unittest
import zlib

from build_lxh_gpk import KEY, PACK_MARKER, TRAILER_MARKER, write_index_entry
from build_lxh_locale_gpks import GpkReader, STATE_ROOT
import convert_chinese_script_gpk as mod


SCENE = "English/00/00-00-A00.ENG.ORS"
SOURCE_NAME = "00/00-00-a00.ors"


def scene_en(*, ending: str = "00:00:03", bg: str = "Movie/example") -> bytes:
    return ("[SkipFRAME]=00:00:20;\r\n"
            f"[PlayMovie]=00:00:00\t{bg}\t0\t00:00:01;\r\n"
            f"[PrintText]=00:00:01\tMakoto\tRetail dialogue.\t{ending};\r\n"
            "[SetSELECT]=00:00:04\tYes\tNo\t00:00:05;\r\n"
            "[Next]=00:00:06;\r\n").encode("utf-8")


def scene_cn(*, ending: str = "00:00:03", bg: str = "Movie/example") -> bytes:
    return ("[SkipFRAME]=\"00:00:20\";\r\n"
            f"[PlayMovie]=\"00:00:00, {bg}, 0, 00:00:01\";\r\n"
            f"[PrintText]=\"00:00:01, 誠, 中文, dialogue stays, {ending}\";\r\n"
            "[SetSELECT]=\"00:00:04, 好, 不, 00:00:05\";\r\n"
            "[Next]=\"00:00:06\";\r\n"
            ";\r\n").encode("gb18030")


def make_pack(path: Path, entries: dict[str, bytes],
              *, original_length_zero: bool = False) -> None:
    """Write only synthetic GPKs. The Chinese fixture mimics raw length=0."""
    with path.open("xb") as out:
        index = bytearray()
        for name, data in entries.items():
            offset = out.tell()
            out.write(data)
            if original_length_zero:
                wide = name.encode("utf-16le")
                index.extend(struct.pack("<H", len(wide) // 2) + wide)
                index.extend(struct.pack("<HHHII4sIB", 0, 0, 0, offset,
                                         len(data), b"    ", 0, 0))
            else:
                index.extend(write_index_entry(name, offset, len(data)))
        index.extend(b"\0\0\0\0")
        encoded_index = struct.pack("<I", len(index)) + zlib.compress(bytes(index))
        out.write(bytes(value ^ KEY[i & 15] for i, value in enumerate(encoded_index)))
        out.write(TRAILER_MARKER + struct.pack("<I", len(encoded_index)) + PACK_MARKER)


class ChineseConverterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        STATE_ROOT.mkdir(parents=True, exist_ok=True)

    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="synthetic-chinese-gpk-", dir=STATE_ROOT))

    def tearDown(self):
        shutil.rmtree(self.root)

    def fixtures(self, chinese: bytes | None = None, retail: bytes | None = None):
        chinese_file, retail_file = self.root / "Script - chinese.GPK", self.root / "Packs" / "Script.GPK"
        retail_file.parent.mkdir(parents=True, exist_ok=True)
        make_pack(chinese_file, {SOURCE_NAME: scene_cn() if chinese is None else chinese},
                  original_length_zero=True)
        make_pack(retail_file, {".DS_STORE": b"metadata-data",
                                "English/.DS_STORE": b"more-metadata",
                                SCENE: scene_en() if retail is None else retail})
        return GpkReader(chinese_file), GpkReader(retail_file)

    def test_valid_chinese_comma_dialogue_is_utf8_native_preserving_text_and_ticks(self):
        original = scene_cn()
        output = mod.convert_scene(original, SOURCE_NAME)
        self.assertIn("中文, dialogue stays".encode("utf-8"), output)
        self.assertIn(b"\tMakoto\t", output)
        self.assertIn(b"[SetSELECT]=00:00:04\t", output)
        self.assertIn(b"[Next]=00:00:06;", output)
        self.assertNotIn(b"Retail dialogue.", output)
        self.assertEqual(len(mod.parse_native(output, "fixture")), 5)
        self.assertEqual(original.decode("gb18030").count("["), 5)

    def test_raw_zero_length_index_and_complete_synthetic_roundtrip(self):
        chinese, retail = self.fixtures()
        self.assertEqual(chinese.entries[SOURCE_NAME].original_length, 0)
        self.assertEqual(mod.read_raw_chinese(chinese, SOURCE_NAME), scene_cn())
        self.assertEqual(mod.retail_original_names(retail), [
            ".DS_STORE", "English/.DS_STORE", SCENE])
        inspected = mod.audit(chinese, retail, verify_provenance=False)
        self.assertEqual(inspected.report["status"], "compatible-not-installed")
        self.assertEqual(inspected.report["fullyCompatibleScenes"], 1)
        self.assertEqual(inspected.report["independentLineCensus"]["printTextCountMismatchScenes"], 0)
        self.assertEqual(inspected.report["independentLineCensus"]["setSELECTCountMismatchScenes"], 0)
        self.assertEqual(set(inspected.files), {SCENE.casefold()})
        output = self.root / "Script - chinese-compatible.GPK"
        mod.write_compatible_gpk(output, inspected.files, retail)
        packed = GpkReader(output)
        self.assertEqual(len(packed.entries), 3)
        self.assertEqual(mod.retail_original_names(packed), mod.retail_original_names(retail))
        self.assertEqual(packed.read_ors(SCENE), inspected.files[SCENE.casefold()])
        self.assertNotIn(b"Retail dialogue.", packed.read_ors(SCENE))
        with self.assertRaises(FileExistsError):
            mod.write_compatible_gpk(output, inspected.files, retail)

    def test_mismatched_timing_and_nontext_payload_fail_closed(self):
        chinese, retail = self.fixtures(retail=scene_en(ending="00:00:04"))
        checked = mod.audit(chinese, retail, verify_provenance=False)
        self.assertEqual(checked.report["status"], "blocked")
        self.assertIn("timing-drift", checked.report["issueCounts"])
        self.assertFalse(checked.files)
        with self.assertRaises(mod.ChineseCompatibilityError):
            mod.write_compatible_gpk(self.root / "Script - chinese-compatible.GPK",
                                     checked.files, retail)
        self.assertFalse((self.root / "Script - chinese-compatible.GPK").exists())
        translated = mod.convert_scene(scene_cn(bg="Movie/altered"), SOURCE_NAME)
        self.assertIn("nontext media/control arguments differ from retail",
                      mod.compare_scene(translated, scene_en(), SOURCE_NAME))

    def test_unrecognized_speaker_blocked_without_leaking_text(self):
        source = scene_cn().replace("誠".encode("gb18030"), "未知".encode("gb18030"))
        with self.assertRaisesRegex(mod.ChineseCompatibilityError, "unmapped Chinese speaker"):
            mod.convert_scene(source, "synthetic")
        chinese, retail = self.fixtures(chinese=source)
        checked = mod.audit(chinese, retail, verify_provenance=False)
        self.assertEqual(checked.report["issueCounts"]["unmapped-speaker"], 1)
        self.assertFalse(checked.files)
        self.assertNotIn("未知", str(checked.report))

    def test_malformed_syntax_must_not_discard_or_retime_commands(self):
        malformed = (
            '[PrintText]="00:00:01, 誠, 字, 00:00:02"',
            '[PrintText]="00:00:01, 誠, 字, 00:00:AA";',
            '[PrintText]="00:00:01, 誠, 字";',
            '尸[SkipFRAME]="00:00:00";',
            '[UnknownCommand]="00:00:00";',
        )
        for line in malformed:
            with self.subTest(line=line):
                with self.assertRaises(mod.ChineseCompatibilityError):
                    mod.convert_line(line, "synthetic")
        with self.assertRaises(mod.ChineseCompatibilityError):
            mod.convert_scene('[PrintText]="00:00:01, 誠, line 1\nline 2, 00:00:02";'.encode("gb18030"), "synthetic")

    def test_retail_noncanonical_frame_timestamp_is_not_silently_normalized(self):
        line = '[PrintText]="00:00:01, 誠, 字, 00:00:30";'
        rendered = mod.convert_line(line, "synthetic")
        self.assertIn("00:00:30", rendered)
        event = mod.parse_native(rendered.encode("utf-8"), "synthetic")[0]
        self.assertEqual(event.end, 30)

    def test_extra_scene_is_a_hard_gate_even_with_a_compatible_scene(self):
        chinese, retail = self.fixtures()
        make_pack(self.root / "chinese-extra.GPK", {
            SOURCE_NAME: scene_cn(), "03/04-kc-b04.ors": scene_cn()},
            original_length_zero=True)
        checked = mod.audit(GpkReader(self.root / "chinese-extra.GPK"), retail,
                            verify_provenance=False)
        self.assertEqual(checked.report["issueCounts"]["extra-scene"], 1)
        self.assertEqual(checked.report["status"], "blocked")
        self.assertFalse(checked.files)

    def test_output_is_restricted_to_qa_and_never_replaced(self):
        with self.assertRaisesRegex(mod.ChineseCompatibilityError, "tools/qa-lab/state"):
            mod.in_state(self.root.parent.parent / "Packs" / "Script.GPK")
        self.assertEqual(mod.in_state(self.root / "Script - chinese-compatible.GPK").parent,
                         self.root.resolve())
        with self.assertRaisesRegex(mod.ChineseCompatibilityError, "SHA-256 does not match"):
            mod.require_original_retail(GpkReader(self.fixtures()[1].path))


if __name__ == "__main__":
    unittest.main()
