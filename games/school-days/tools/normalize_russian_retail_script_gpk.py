"""Repair one proven retail-parity TAB defect in the community Russian GPK.

The original archives are READ-ONLY. Output is a new, fully verified GPK under
tools/qa-lab/state. This tool never translates text, inserts English fallback,
changes timing or installs a pack. It restores the original retail parser's
handling of one malformed PrintText record in 03-KB-D10.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import hashlib
import json
import uuid

from build_lxh_locale_gpks import GpkReader, RETAIL_ENGLISH_SHA256, STATE_ROOT
from normalize_spanish_script_gpk import source_member_bytes, write_full_archive

SOURCE_SHA256 = "1f67dda936ffec15b5543b1578b2c79b6666371a21765273f864a5c6eaba8394"
SCENE = "english/03/03-kb-d10.eng.ors"
BAD_PREFIX = b"[PrintText]=00:16:19\tTaisuke\t"
END_TICK = b"\t00:24:01;"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_raw_metadata(reader: GpkReader, key: str) -> bytes:
    item = reader.entries[key]
    if item.magic != b"    " or item.stored_length < len(item.prefix):
        raise ValueError("Unexpected non-ORS metadata member layout")
    with reader.path.open("rb") as stream:
        stream.seek(item.offset)
        payload = stream.read(item.stored_length - len(item.prefix))
    if len(payload) != item.stored_length - len(item.prefix):
        raise ValueError("Truncated GPK metadata entry")
    return item.prefix + payload


def normalize(retail_path: Path, russian_path: Path):
    retail = GpkReader(retail_path)
    russian = GpkReader(russian_path)
    if (retail_path.name != "Script.GPK" or retail_path.parent.name.lower() != "packs"
            or sha256(retail_path) != RETAIL_ENGLISH_SHA256):
        raise ValueError("Retail authority must be the unmodified Packs/Script.GPK")
    if russian_path.name != "Script - Russian.GPK" or sha256(russian_path) != SOURCE_SHA256:
        raise ValueError("Unknown Russian source file; do not normalize without a fresh audit")
    if (len(retail.entries) != 1859 or len(russian.entries) != 1859
            or retail.original_names != russian.original_names):
        raise ValueError("Russian/retail PIDX differs; normalization refused")

    original_lines = retail.read_ors(SCENE).splitlines(keepends=True)
    translated_lines = russian.read_ors(SCENE).splitlines(keepends=True)
    if len(original_lines) != len(translated_lines):
        raise ValueError("03-KB-D10 source line count differs")
    old = original_lines[28].rstrip(b"\r\n")
    localized = translated_lines[28].rstrip(b"\r\n")
    if (not old.startswith(BAD_PREFIX) or old.count(b"\t") != 4
            or not old.endswith(b"\t\t00:24:01;")
            or not localized.startswith(BAD_PREFIX) or localized.count(b"\t") != 3
            or not localized.endswith(END_TICK)):
        raise ValueError("Unexpected 03-KB-D10 syntax; cannot safely repair")
    translated_text = localized[len(BAD_PREFIX):-len(END_TICK)].decode("utf-8")
    if not translated_text or not any("\u0400" <= c <= "\u04ff" for c in translated_text):
        raise ValueError("Missing Russian translation in the only repaired line")
    translated_lines[28] = translated_lines[28].replace(END_TICK, b"\t\t00:24:01;", 1)
    repaired_scene = b"".join(translated_lines)
    if repaired_scene.count(BAD_PREFIX) != 1:
        raise ValueError("Duplicate altered line")

    files = {}
    for key in russian.entries:
        exact_name = russian.original_names[key]
        if key == SCENE:
            data = repaired_scene
        elif key.endswith(".ors"):
            data = russian.read_ors(key)
        else:
            data = source_member_bytes(russian, key)
        files[exact_name] = data
    return files, translated_text


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--russian", type=Path, required=True)
    args = parser.parse_args()

    retail = args.retail.resolve(strict=True)
    russian = args.russian.resolve(strict=True)
    files, line = normalize(retail, russian)
    state = STATE_ROOT.resolve()
    folder = state / "russian-retail-normalized" / ("run-" + uuid.uuid4().hex)
    folder.mkdir(parents=True, exist_ok=False)
    destination = folder / "Script - Russian.GPK"
    details = write_full_archive(destination, files)
    roundtrip = GpkReader(destination)
    source = GpkReader(russian)
    if roundtrip.original_names != source.original_names:
        raise ValueError("Normalized GPK PIDX mismatch")
    for key in source.entries:
        name = source.original_names[key]
        expected = files[name]
        actual = (roundtrip.read_ors(key) if key.endswith(".ors")
                  else read_raw_metadata(roundtrip, key))
        if actual != expected:
            raise ValueError("Normalized GPK roundtrip mismatch: " + name)
    report = {
        "sourceSha256": SOURCE_SHA256,
        "retailSha256": RETAIL_ENGLISH_SHA256,
        "normalizedSha256": details["sha256"],
        "members": len(files),
        "changedScenes": [source.original_names[SCENE]],
        "changedBehavior": "Restored original retail parser behavior for one malformed 5-field PrintText",
        "translationPreserved": True,
        "notes": "The corrected Russian line remains authored but is ignored just like its retail counterpart; no English fallback",
    }
    (folder / "audit.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("RUSSIAN_RETAIL_NORMALIZATION_PASS", len(files), details["sha256"])
    print("GPK", destination)
    print("AUDIT", folder / "audit.json")


if __name__ == "__main__":
    main()
