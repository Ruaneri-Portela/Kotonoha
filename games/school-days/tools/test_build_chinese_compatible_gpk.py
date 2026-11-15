"""Synthetic-only regression checks for the Chinese retail-authoritative GPK builder."""
from __future__ import annotations

from collections import Counter
import unittest

from build_chinese_compatible_gpk import patch_scene
from build_lxh_locale_gpks import LocaleGpkError
from normalize_spanish_script_gpk import NativeOrs


SOURCE = (
    b"[SkipFRAME]=00:00:00;\r\n"
    b"[PlayVoice]=00:00:01\tVoice/synthetic\t0\tmak\t00:00:04;\r\n"
    b"[PrintText]=00:00:01\tMakoto\tEnglish sentence.\t00:00:04;\r\n"
    b"[SetSELECT]=00:00:05\tChoice A\tChoice B\t00:00:06;\r\n"
    b"[Next]=00:00:07;\r\n"
)


class ChineseRebuildTests(unittest.TestCase):
    def test_all_text_and_choice_slots_change_but_controls_stay_identical(self):
        stats = Counter()
        translated = patch_scene(SOURCE, {
            "English sentence.": "中文台词。",
            "Choice A": "选项一",
            "Choice B": "选项二",
        }, 123, stats)
        old, new = NativeOrs(SOURCE), NativeOrs(translated)
        self.assertEqual([(e.kind, e.start, e.end) for e in old.events],
                         [(e.kind, e.start, e.end) for e in new.events])
        for before, after in zip(old.events, new.events):
            if before.kind not in ("PrintText", "SetSELECT"):
                self.assertEqual(old.lines[before.index], new.lines[after.index])
        self.assertNotIn(b"English sentence.", translated)
        self.assertNotIn(b"Choice A", translated)
        self.assertNotIn(b"Choice B", translated)
        self.assertEqual(stats["printText"], 1)
        self.assertEqual(stats["choices"], 2)

    def test_missing_or_identity_or_foreign_credit_rejected(self):
        with self.assertRaisesRegex(LocaleGpkError, "Missing Chinese HQ translation"):
            patch_scene(SOURCE, {"English sentence.": "中文"}, 123, Counter())
        with self.assertRaisesRegex(LocaleGpkError, "Untranslated Chinese HQ slot"):
            patch_scene(SOURCE, {
                "English sentence.": "English sentence.",
                "Choice A": "选项一", "Choice B": "选项二"
            }, 123, Counter())
        with self.assertRaisesRegex(LocaleGpkError, "Foreign-language translation credit"):
            patch_scene(SOURCE, {
                "English sentence.": "Juan Ochoa",
                "Choice A": "选项一", "Choice B": "选项二"
            }, 123, Counter())


if __name__ == "__main__":
    unittest.main()
