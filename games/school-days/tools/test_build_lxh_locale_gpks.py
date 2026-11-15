"""Synthetic-only tests for the private LxH locale GPK composer."""

from __future__ import annotations

from pathlib import Path
import shutil
import tempfile
import unittest

import build_lxh_locale_gpks as mod


def stamp(tick: int) -> str:
    minutes, rest = divmod(tick, 24 * 60)
    seconds, frames = divmod(rest, 24)
    return f"{minutes:02d}:{seconds:02d}:{frames:02d}"


def fake_scene(scene: str, ending: str, keys: set[str]) -> tuple[bytes, bytes, dict]:
    slots = sorted(int(key.split(":")[1]) for key in keys)
    if not slots or len(slots) != len(keys):
        raise ValueError("Invalid synthetic keys")
    movie_index = slots[0] - 1
    if movie_index < 5:
        raise ValueError("Synthetic test needs a nonempty HQ prefix")
    source = []
    base = []
    for ordinal in range(movie_index):
        begin, end = stamp(ordinal), stamp(ordinal)
        if ordinal == 0:
            value = f"[SkipFRAME]={begin};"
            source.append(value)
            base.append(value)
        elif ordinal == 1:
            source.append(f"[PrintText]={begin}\tSekai\tOriginal English HQ.\t{end};")
            base.append(f"[PrintText]={begin}\tSekai\tLocalized HQ for {scene}.\t{end};")
        elif ordinal == movie_index - 2:
            value = f"[EndRoll]={begin}\tSyntheticCredits\t{end};"
            source.append(value)
            base.append(value)
        elif ordinal == movie_index - 1:
            # New pre-movie fade exists only in the graft, not in HQ base.
            source.append(f"[BlackFade]={begin}\tIN\t{end};")
        else:
            value = f"[BlackFade]={begin}\tOUT\t{end};"
            source.append(value)
            base.append(value)
    source.append(
        f"[PlayMovie]={stamp(movie_index)}\tLxH/Media/{ending}-LXH\t0\t"
        f"{stamp(movie_index)};")
    translated = {}
    for ordinal in range(slots[0], slots[-1] + 1):
        begin = end = stamp(ordinal)
        if ordinal in slots:
            source.append(f"[PrintText]={begin}\tSekai\tOld graft line.\t{end};")
            translated[f"@ors:{ordinal}:0"] = f"LxH synthetic {scene}/{ordinal}^"
        else:
            source.append(f"[BlackFade]={begin}\tIN\t{end};")
    source.append(f"[Next]={stamp(slots[-1] + 1)};")
    base.append(f"[Next]={stamp(movie_index)};")
    return ("\r\n".join(source) + "\r\n").encode("utf-8"), (
        "\r\n".join(base) + "\r\n").encode("utf-8"), translated


class LocaleGpkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Never create fixture packs outside QA state.
        mod.STATE_ROOT.mkdir(parents=True, exist_ok=True)

    def setUp(self):
        self.directory = Path(tempfile.mkdtemp(
            prefix="lxh-locale-test-", dir=mod.STATE_ROOT))

    def tearDown(self):
        # Only this test's independently created directory.
        shutil.rmtree(self.directory)

    def test_all_273_actual_ordinal_shapes_from_synthetic_ors(self):
        translations = mod.load_sidecars(
            mod.REPO_ROOT / "build" / "school-days-dev-control4" /
            "games" / "school-days" / "Release" / "translations",
            ("en", "pt-BR", "zh-CN", "es"))
        self.assertEqual(sum(len(translations["en"][scene])
                             for scene, _, _ in mod.EXPECTED), 273)
        self.assertEqual(sum(len(translations["es"][scene])
                             for scene, _, _ in mod.EXPECTED), 273)
        self.assertEqual(sum(
            value == "\u200b" for scene, _, _ in mod.EXPECTED
            for value in translations["es"][scene].values()), 7)
        files = {}
        base_files = {}
        fake_translations = {}
        for scene, ending, count in mod.EXPECTED:
            source, base, strings = fake_scene(
                scene, ending, set(translations["en"][scene]))
            self.assertEqual(len(strings), count)
            composed, report = mod.compose_scene(
                ending, scene, count, source, base, strings)
            self.assertEqual(report["lxhPrintText"], count)
            self.assertIn(f"Localized HQ for {scene}".encode(), composed)
            self.assertNotIn(b"Original English HQ.", composed)
            self.assertNotIn(b"Old graft line.", composed)
            files[mod.gpk_logical(ending)] = composed
            base_files[mod.base_logical(ending)] = base
            fake_translations[scene] = strings
        path = self.directory / "synthetic-seven.gpk"
        report = mod.write_private_gpk(path, files)
        self.assertEqual(report["name"], "synthetic-seven.gpk")
        reader = mod.GpkReader(path)
        self.assertEqual(len(reader.entries), 7)
        self.assertEqual(
            [len(mod.Ors(reader.read_ors(mod.gpk_logical(ending)), ending).events)
             for _, ending, _ in mod.EXPECTED],
            [len(mod.Ors(files[mod.gpk_logical(ending)], ending).events)
             for _, ending, _ in mod.EXPECTED])
        base_pack = self.directory / "synthetic-original.gpk"
        mod.write_private_gpk(base_pack, base_files)
        self.assertEqual(len(mod.GpkReader(base_pack).entries), 7)

    def test_language_gpk_sources_keep_hq_and_have_locale_names(self):
        translated = {}
        base_files = {}
        extended_files = {}
        for scene, ending, count in mod.EXPECTED:
            # Dedicated synthetic ORS ordinals need not match the real 273
            # corpus here; they check the source-reader and publisher paths.
            source, base, dictionary = fake_scene(
                scene, ending, {f"@ors:{i}:0" for i in range(8, 8 + count)})
            extended_files[mod.gpk_logical(ending)] = source
            base_files[mod.base_logical(ending)] = base
            translated[scene] = dictionary
        mod.write_private_gpk(self.directory / "source-lxh.gpk", extended_files)
        mod.write_private_gpk(self.directory / "source-original.gpk", base_files)
        lxh = mod.Source(gpk=self.directory / "source-lxh.gpk")
        base = mod.Source(gpk=self.directory / "source-original.gpk")
        sidecar = {"en": translated, "pt-BR": translated, "es": translated}
        result = {}
        # A synthetic seven-entry base is not a verified retail pack, so it
        # must be rejected by both production EN/PT-BR provenance guards.
        with self.assertRaisesRegex(mod.LocaleGpkError, "original Packs/Script"):
            mod.build_locale("en", lxh, base, sidecar)
        with self.assertRaisesRegex(mod.LocaleGpkError, "Script - Brazilian"):
            mod.build_locale("pt-BR", lxh, base, sidecar)
        with self.assertRaisesRegex(mod.LocaleGpkError, "certified Script - Spanish"):
            mod.build_locale("es", lxh, base, sidecar)
        # Exercise the output naming only using synthetic, directly composed
        # events; do not pretend these stand in for validated HQ provenance.
        synthetic_files = {}
        reports = []
        for scene, ending, count in mod.EXPECTED:
            composed, report = mod.compose_scene(
                ending, scene, count, lxh.read(mod.gpk_logical(ending)),
                base.read(mod.base_logical(ending)), translated[scene])
            synthetic_files[mod.gpk_logical(ending)] = composed
            reports.append(report)
        result["en"] = (synthetic_files, reports)
        result["pt-BR"] = (synthetic_files, reports)
        result["es"] = (synthetic_files, reports)
        for locale in result:
            self.assertEqual(sum(x["lxhPrintText"] for x in result[locale][1]), 273)
        out = mod.publish(result, self.directory)
        self.assertEqual(
            {p.name for p in out.glob("*.GPK")},
            {"en-lxh.GPK", "pt-BR-lxh.GPK", "es-lxh.GPK"})
        self.assertTrue((out / "manifest.json").is_file())
        with self.assertRaises(mod.LocaleGpkError):
            mod.build_locale("zh-CN", lxh, base, {"zh-CN": translated})

    def test_error_cases_block_without_publishing(self):
        source, base, strings = fake_scene("999", "05-A2-F00",
                                           {"@ors:6:0", "@ors:7:0"})
        result, _ = mod.compose_scene("05-A2-F00", "999", 2,
                                      source, base, strings)
        self.assertIn(b"Localized HQ for 999", result)
        with self.assertRaisesRegex(mod.LocaleGpkError, "missing translation"):
            mod.compose_scene("05-A2-F00", "999", 2, source, base,
                              {"@ors:6:0": "Only one"})
        with self.assertRaisesRegex(mod.LocaleGpkError, "field separator"):
            altered = dict(strings)
            altered["@ors:6:0"] = "unescaped\nnewline"
            mod.compose_scene("05-A2-F00", "999", 2, source, base, altered)
        with self.assertRaisesRegex(mod.LocaleGpkError, "HQ event mismatch"):
            wrong_base = base.replace(b"[PrintText]=", b"[CreateBG]=")
            mod.compose_scene("05-A2-F00", "999", 2, source, wrong_base, strings)
        with self.assertRaisesRegex(mod.LocaleGpkError, "charset"):
            mod.Ors(b"[PrintText]=00:00:00\tSekai\t\xca\xca\t00:00:01;\n",
                    "synthetic Chinese")
        with self.assertRaisesRegex(mod.LocaleGpkError, "TAB field count"):
            mod.Ors(b"[PrintText]=00:00:00,Sekai,wrong,00:00:01;\n",
                    "synthetic Chinese CSV")
        with self.assertRaisesRegex(mod.LocaleGpkError, "must remain under"):
            mod.publish({}, Path(tempfile.gettempdir()))
        self.assertFalse(list(self.directory.rglob("*.GPK")))


if __name__ == "__main__":
    unittest.main()
