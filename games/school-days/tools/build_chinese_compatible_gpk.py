"""Rebuild the Chinese HQ script with retail commands and verified Chinese text.

The 1859-member Chinese patch is GB18030 and changes event order/scene counts.
It cannot be mounted directly by Kotonoha. This offline conversion keeps *all*
retail ORS commands, timings, choices, voices and paths, replacing every
displayed HQ text slot from the existing complete zh-CN.json. The sole
KTRF-unreferenced Japanese line comes from the corresponding Chinese GPK
event with an exact scene/tick match. No English dialogue fallback is used.

Outputs are created only in tools/qa-lab/state/, never in Packs/ or a live
Release, and the commercial source archives are opened READ ONLY.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
from pathlib import Path
import sys
import uuid

from build_lxh_locale_gpks import (
    GpkReader, LocaleGpkError, RETAIL_ENGLISH_SHA256, STATE_ROOT,
    digest_file_stream,
)
from convert_chinese_script_gpk import convert_scene, read_raw_chinese
from normalize_spanish_script_gpk import (
    EXPECTED_CHOICES, EXPECTED_RETAIL_ENTRIES, EXPECTED_TEXT,
    LEGACY_UNMAPPED_SCENE, OTHER_UNMAPPED_SCENE, NativeOrs,
    normalize_line_breaks, read_json, read_ktrf_scenes,
    source_member_bytes, verify_file, write_full_archive,
)

CHINESE_SOURCE_SHA256 = (
    "5de8561d70405bccd2394b76d98b06a689142116ef05c4130cb14e8091f6ad16"
)
EXPECTED_TRANSLATED_SLOTS = EXPECTED_TEXT + EXPECTED_CHOICES
EXPECTED_KTRF_SCENES = 1855


def patch_scene(original: bytes, values: dict, node: int, stats: Counter) -> bytes:
    scene = NativeOrs(original)
    if not isinstance(values, dict):
        raise LocaleGpkError(f"Missing Chinese translations for NODE {node}")
    for ordinal, slot, event, field, raw in scene.printed_slots():
        english = raw.decode("utf-8", "strict")
        context = f"@ors:{ordinal}:{slot}"
        chinese = values.get(context, values.get(english))
        if not isinstance(chinese, str) or not chinese:
            raise LocaleGpkError(f"Missing Chinese HQ translation NODE {node} {context}")
        # A source-identical line must be audited; none is present in the
        # authenticated 30,968-slot zh-CN JSON used to create this derivative.
        if chinese == english:
            raise LocaleGpkError(f"Untranslated Chinese HQ slot NODE {node} {context}")
        if "ochoa" in chinese.casefold() or "uchoa" in chinese.casefold():
            raise LocaleGpkError(f"Foreign-language translation credit NODE {node}")
        chinese, newline = normalize_line_breaks(chinese)
        stats["escapedNewlines"] += int(newline)
        stats["contextual"] += int(context in values)
        stats["choices" if event.kind == "SetSELECT" else "printText"] += 1
        scene.put_field(event, field, chinese)
    return scene.finish()


def patch_unreferenced(original: bytes, chinese: GpkReader) -> bytes:
    """Translate the only non-KTRF spoken line with a unique raw-patch match."""
    source_key = "05/05-9o-b00.ors"
    if source_key not in chinese.entries:
        raise LocaleGpkError("Chinese patch lacks the unreferenced HQ scene")
    raw = read_raw_chinese(chinese, source_key)
    unknown_speakers: set[str] = set()
    converted = convert_scene(raw, source_key, unknown_speakers=unknown_speakers)
    source_slots = list(NativeOrs(converted).printed_slots())
    base = NativeOrs(original)
    targets = list(base.printed_slots())
    if len(targets) != 1:
        raise LocaleGpkError("Unexpected unreferenced retail HQ text inventory")
    ordinal, slot, event, field, english = targets[0]
    if (event.kind != "PrintText" or event.start != 3626 or slot != 0 or
            event.fields[1] != b"Makoto"):
        raise LocaleGpkError("Unreferenced retail HQ scene/tick/speaker changed")
    matches = [text.decode("utf-8") for _, _, source_event, _, text in source_slots
               if source_event.kind == "PrintText" and
               source_event.start == event.start and
               source_event.fields[1] == event.fields[1]]
    if len(matches) != 1 or matches[0] == english.decode("utf-8"):
        raise LocaleGpkError("No unique Chinese translation for unreferenced HQ line")
    base.put_field(event, field, matches[0])
    return base.finish()


def build(retail_path: Path, chinese_path: Path, json_path: Path,
          route_path: Path) -> tuple[dict[str, bytes], Counter]:
    retail_path, chinese_path = verify_file(retail_path), verify_file(chinese_path)
    if (retail_path.name != "Script.GPK" or retail_path.parent.name != "Packs" or
            digest_file_stream(retail_path) != RETAIL_ENGLISH_SHA256):
        raise LocaleGpkError("Original retail Packs/Script.GPK SHA-256 differs")
    if (chinese_path.name != "Script - chinese.GPK" or
            digest_file_stream(chinese_path) != CHINESE_SOURCE_SHA256):
        raise LocaleGpkError("Original GB18030 Chinese Script.GPK SHA-256 differs")
    retail = GpkReader(retail_path)
    chinese = GpkReader(chinese_path)
    if (len(retail.entries) != EXPECTED_RETAIL_ENTRIES or
            len(chinese.entries) != EXPECTED_RETAIL_ENTRIES):
        raise LocaleGpkError("Retail/Chinese 1859-member inventory differs")
    routes = read_ktrf_scenes(route_path)
    dictionary = read_json(json_path)
    if (len(routes) != EXPECTED_KTRF_SCENES or dictionary.get("nodo") != {} or
            set(dictionary) != {str(i) for i in routes.values()} | {"nodo"}):
        raise LocaleGpkError("Chinese JSON scene coverage differs from KTRF")

    files: dict[str, bytes] = {}
    stats = Counter()
    for canonical, exact in retail.original_names.items():
        if not canonical.endswith(".ors"):
            files[exact] = source_member_bytes(retail, canonical)
            continue
        original = retail.read_ors(canonical)
        if canonical in routes:
            node = routes[canonical]
            translated = patch_scene(original, dictionary[str(node)], node, stats)
        elif canonical == LEGACY_UNMAPPED_SCENE:
            translated = patch_unreferenced(original, chinese)
            stats["unreferencedChinese"] += 1
        elif canonical == OTHER_UNMAPPED_SCENE:
            if list(NativeOrs(original).printed_slots()):
                raise LocaleGpkError("Unreferenced HQ control-only scene changed")
            translated = original
        else:
            raise LocaleGpkError(f"Unexpected retail ORS: {canonical}")

        source, final = NativeOrs(original), NativeOrs(translated)
        if [(x.kind, x.start, x.end) for x in source.events] != [
                (x.kind, x.start, x.end) for x in final.events]:
            raise LocaleGpkError(f"Chinese rebuild altered ORS event/timing: {canonical}")
        for a, b in zip(source.events, final.events):
            if a.kind not in ("PrintText", "SetSELECT") and \
                    source.lines[a.index] != final.lines[b.index]:
                raise LocaleGpkError(f"Chinese rebuild altered control/media: {canonical}")
        files[exact] = translated
        stats["scenes"] += 1

    if (len(files) != EXPECTED_RETAIL_ENTRIES or stats["scenes"] != 1857 or
            stats["printText"] != EXPECTED_TEXT or
            stats["choices"] != EXPECTED_CHOICES or
            stats["unreferencedChinese"] != 1):
        raise LocaleGpkError(f"Chinese HQ reconstruction coverage failed: {stats}")
    return files, stats


def publish(files: dict[str, bytes], stats: Counter) -> Path:
    out = STATE_ROOT.resolve(strict=True) / "chinese-compatible" / (
        "run-" + uuid.uuid4().hex)
    out.mkdir(parents=True, exist_ok=False)
    temporary = out / ".private-chinese.GPK"
    final = out / "Script - chinese-compatible.GPK"
    try:
        result = write_full_archive(temporary, files)
        if len(GpkReader(temporary).entries) != EXPECTED_RETAIL_ENTRIES:
            raise LocaleGpkError("Reconstructed Chinese GPK readback count differs")
        temporary.rename(final)
        (out / "audit.json").write_text(
            __import__("json").dumps({
                "status": "private-not-installed", "locale": "zh-CN",
                "retailControlAuthority": RETAIL_ENGLISH_SHA256,
                "rawChineseSource": CHINESE_SOURCE_SHA256,
                "hqTexts": EXPECTED_TRANSLATED_SLOTS,
                "sha256": result["sha256"], "bytes": result["bytes"],
                "counts": dict(sorted(stats.items())),
                "note": "All text translated; all control/media from unchanged retail."
            }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        return final
    finally:
        temporary.unlink(missing_ok=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--retail-gpk", type=Path, required=True)
    parser.add_argument("--chinese-gpk", type=Path, required=True)
    parser.add_argument("--zh-json", type=Path, required=True)
    parser.add_argument("--ktrf", type=Path, required=True)
    parser.add_argument("--build", action="store_true", help="Create QA-only output")
    args = parser.parse_args(argv)
    try:
        files, stats = build(args.retail_gpk, args.chinese_gpk,
                             args.zh_json, args.ktrf)
        print("ZH_COMPATIBLE_GPK_AUDIT_PASS", dict(stats))
        if args.build:
            print("PRIVATE_OUTPUT", publish(files, stats))
        else:
            print("DRY_RUN; no output created")
        return 0
    except (LocaleGpkError, OSError, ValueError, UnicodeError) as exc:
        print("ZH_COMPATIBLE_GPK_BLOCKED:", exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
