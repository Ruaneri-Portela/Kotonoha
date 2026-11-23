#!/usr/bin/env python3
"""Verify 100% key and format-placeholder parity across all 5 Android launcher locales."""

from __future__ import annotations

from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

REQUIRED_LOCALES = {
    "en": "values",
    "pt-BR": "values-pt-rBR",
    "es": "values-es",
    "ru": "values-ru",
    "zh-CN": "values-zh-rCN",
}

FORMAT_SPEC = re.compile(r"%\d+\$[sd]|%%")


def parse_strings(path: Path) -> dict[str, str]:
    root = ET.parse(path).getroot()
    out: dict[str, str] = {}
    for elem in root.findall("string"):
        name = elem.attrib["name"]
        text = (elem.text or "").strip()
        if not text:
            raise AssertionError(f"Empty string '{name}' in {path}")
        out[name] = text
    return out


def main() -> int:
    repo_root = Path(__file__).resolve().parents[2]
    res_dir = repo_root / "buildsystems/android/app/src/schoolDays/res"
    tables: dict[str, dict[str, str]] = {}
    for code, folder in REQUIRED_LOCALES.items():
        xml_path = res_dir / folder / "strings.xml"
        if not xml_path.is_file():
            print(f"ERROR: Missing Android strings.xml for {code}: {xml_path}", file=sys.stderr)
            return 1
        tables[code] = parse_strings(xml_path)

    base = tables["en"]
    base_keys = set(base)
    for code, table in tables.items():
        missing = sorted(base_keys - set(table))
        extra = sorted(set(table) - base_keys)
        if missing or extra:
            print(
                f"ERROR: Locale {code} key mismatch: missing={missing}, extra={extra}",
                file=sys.stderr,
            )
            return 1
        for key, en_val in base.items():
            en_specs = sorted(FORMAT_SPEC.findall(en_val))
            loc_specs = sorted(FORMAT_SPEC.findall(table[key]))
            if en_specs != loc_specs:
                print(
                    f"ERROR: Format specifier mismatch for {code}/{key}: "
                    f"{loc_specs} != {en_specs}",
                    file=sys.stderr,
                )
                return 1

    print(
        f"PASS Android SchoolDaysSetup strings parity: "
        f"{len(REQUIRED_LOCALES)} locales x {len(base_keys)} keys"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
