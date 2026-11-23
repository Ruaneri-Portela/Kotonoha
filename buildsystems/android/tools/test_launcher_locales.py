"""Validate all Android universal-launcher translations and their references."""

import re
import unittest
from pathlib import Path
from xml.etree import ElementTree


MAIN = Path(__file__).resolve().parents[1] / "app" / "src" / "main"
RES = MAIN / "res"
JAVA = MAIN / "java" / "org" / "kotonoha" / "android" / "LauncherActivity.java"
LOCALES = ("values", "values-pt-rBR", "values-es", "values-ru", "values-zh-rCN")
FORMAT_ARGS = re.compile(r"%(?:\d+\$)?[ds]")
JAVA_RESOURCE = re.compile(r"R\.(string|array|plurals)\.([a-z][a-z0-9_]*)")
ANDROID = "{http://schemas.android.com/apk/res/android}"
APP = "{http://schemas.android.com/apk/res-auto}"


def catalog(locale):
    tree = ElementTree.parse(RES / locale / "strings.xml")
    entries = {}
    for item in tree.getroot():
        if item.tag not in ("string", "string-array", "plurals"):
            continue
        name = item.attrib["name"]
        if name in entries:
            raise ValueError(f"Duplicate resource: {locale}/{name}")
        values = [item.text or ""] if item.tag == "string" else [
            child.text or "" for child in item
        ]
        entries[name] = (item.tag, values, item.get("translatable") != "false")
    return entries


class LauncherLocalesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalogs = {language: catalog(language) for language in LOCALES}

    def test_all_translatable_keys_and_format_arguments_exist(self):
        base = self.catalogs["values"]
        expected = {name for name, entry in base.items() if entry[2]}
        for locale, resources in self.catalogs.items():
            with self.subTest(locale=locale):
                self.assertEqual(expected, set(resources) - {
                    name for name, entry in base.items() if not entry[2]
                })
                for name in expected:
                    kind, values, _ = resources[name]
                    self.assertEqual(base[name][0], kind, (locale, name))
                    if kind != "plurals":
                        self.assertEqual(len(base[name][1]), len(values), (locale, name))
                    else:
                        plural = ElementTree.parse(RES / locale / "strings.xml")
                        quantities = [item.get("quantity") for item in plural.findall(
                            ".//plurals[@name='games_detected']/item"
                        )]
                        self.assertIn("other", quantities, locale)
                        self.assertEqual(len(quantities), len(set(quantities)), locale)
                    for index, translated in enumerate(values):
                        original = base[name][1][0 if kind == "plurals" else index]
                        self.assertTrue(translated.strip(), (locale, name))
                        self.assertEqual(
                            FORMAT_ARGS.findall(original),
                            FORMAT_ARGS.findall(translated),
                            (locale, name),
                        )

    def test_language_order_and_option_values(self):
        names = self.catalogs["values"]["launcher_languages"][1]
        self.assertEqual(
            ["English", "Português (Brasil)", "Español", "Русский", "简体中文"], names
        )
        for locale, entries in self.catalogs.items():
            with self.subTest(locale=locale):
                self.assertEqual(names, entries["launcher_languages"][1])
                self.assertEqual(4, len(entries["renderer_labels"][1]))
                self.assertEqual(4, len(entries["upscaler_labels"][1]))

    def test_traditional_chinese_never_auto_selects_simplified(self):
        source = JAVA.read_text(encoding="utf-8")
        self.assertIn('"Hant".equalsIgnoreCase(system.getScript())', source)
        for country in ("TW", "HK", "MO"):
            self.assertIn(f'"{country}".equalsIgnoreCase(system.getCountry())', source)
        self.assertIn('case "zh":', source)

    def test_all_layout_labels_use_resources(self):
        base = self.catalogs["values"]
        for layout in sorted((RES / "layout").glob("*.xml")):
            for element in ElementTree.parse(layout).iter():
                for attr in (
                    ANDROID + "text", ANDROID + "hint", ANDROID + "contentDescription",
                    APP + "title", APP + "subtitle",
                ):
                    value = element.get(attr)
                    if value is None:
                        continue
                    self.assertTrue(value.startswith("@string/"), (layout, value))
                    self.assertIn(value[len("@string/"):], base, (layout, value))

    def test_java_and_layout_resource_references_are_defined(self):
        base = self.catalogs["values"]
        java = JAVA.read_text(encoding="utf-8")
        for kind, name in JAVA_RESOURCE.findall(java):
            expected_kind = {"array": "string-array", "plurals": "plurals"}.get(kind, "string")
            self.assertIn(name, base, (kind, name))
            self.assertEqual(expected_kind, base[name][0], (kind, name))


if __name__ == "__main__":
    unittest.main()
