"""Offline, fail-closed Spanish HQ GPK normalization (QA state ONLY).

Use the *unaltered* retail Packs/Script.GPK as the single source of scene
commands, timings, voices and paths, translate its displayed text using the
existing reviewed es.json V4b, and write Script - Spanish.GPK privately.

The two Spanish NSIS installers are never run. Their raw Script.gpk indexes
omit the canonical PIDX terminator, 1,809 scenes have BOM, and 180 scene
timelines differ from retail: DO NOT use those archives as runtime GPKs.
One KTRF-unreferenced Japanese line is matched explicitly by the private
Spanish HQ patch's unique (scene, tick, speaker) tuple.

No translation JSON is written; this reads es.json and its existing review
manifest offline. No game executable is launched, no source GPK/saves/Release
is written, and no Git actions are performed. The resulting pack is private.
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import uuid
import zlib

from build_lxh_locale_gpks import (
    GpkReader, LocaleGpkError, REPO_ROOT, RETAIL_ENGLISH_SHA256,
    STATE_ROOT, digest_file_stream,
)
from build_lxh_gpk import (
    KEY, PACK_MARKER, TRAILER_MARKER, verify_archive, write_index_entry,
)

LEGACY_SPANISH_HASH = (
    "139b3c77ea0e4c3ac7f1b7b0871912e55c8480e01d2e5d37cd95a203b4038dd4"
)
LEGACY_UNMAPPED_SCENE = "english/05/05-9o-b00.eng.ors"
OTHER_UNMAPPED_SCENE = "english/01/01-00-op2.eng.ors"
EXPECTED_KTRF_NODES = 1857
EXPECTED_SCENE_NODES = 1855
EXPECTED_RETAIL_ENTRIES = 1859
EXPECTED_TEXT = 30483
EXPECTED_CHOICES = 485
EXPECTED_CONTEXT = 184
APPROVED_IDENTITY_REASONS = {
    "proper_name", "punctuation", "interjection", "shared_spanish_word",
    "onomatopoeia", "spanish_original_lxh", "numeric_format",
    "genuinely_bilingual_term", "honorific", "emoticon", "sound_effect",
    "brand_name", "location_name", "nickname", "foreign_name",
}
EXPECTED_NL_ESCAPES = 2
MAX_TRANSLATION_BYTES = 128 * 1024
TICK_RE = re.compile(rb"^(\d+):(\d+):(\d+)$")
KEY_RE = re.compile(r"^@ors:(0|[1-9]\d*):(0|[1-9]\d*)$")

FIELD_COUNTS = {
    "Next": 1, "SkipFRAME": 1, "PlayVoice": 5, "CreateBG": 4,
    "PlaySe": 4, "PlayMovie": 4, "PrintText": 4, "SetSELECT": 4,
    "BlackFade": 3, "WhiteFade": 3, "PlayBgm": 3, "EndBGM": 3,
    "EndRoll": 3, "MoveSom": 3,
}


def verify_file(path: Path) -> Path:
    resolved = path.resolve(strict=True)
    if not path.is_file() or path.is_symlink() or not resolved.is_file():
        raise LocaleGpkError(f"Missing/unsafe source file {path}")
    return resolved


def read_json(path: Path) -> dict:
    path = verify_file(path)
    def distinct(pairs):
        obj = {}
        for key, value in pairs:
            if key in obj:
                raise LocaleGpkError(f"Duplicate JSON key in {path.name}: {key}")
            obj[key] = value
        return obj
    result = json.loads(path.read_text(encoding="utf-8-sig"),
                        object_pairs_hook=distinct)
    if not isinstance(result, dict):
        raise LocaleGpkError(f"JSON root must be an object: {path}")
    return result


def read_ktrf_scenes(path: Path) -> dict[str, int]:
    blob = verify_file(path).read_bytes()
    if len(blob) < 96 or blob[:4] != b"KTRF":
        raise LocaleGpkError("KTRF v0.1 header missing")
    if struct.unpack_from("<HHHH", blob, 4) != (0, 1, 96, 48):
        raise LocaleGpkError("KTRF version/layout unsupported")
    header = bytearray(blob[:96])
    header[44:48] = b"\0\0\0\0"
    if (zlib.crc32(header) & 0xffffffff) != struct.unpack_from("<I", blob, 44)[0]:
        raise LocaleGpkError("KTRF header checksum mismatch")
    if hashlib.sha256(blob[96:]).digest() != blob[48:80]:
        raise LocaleGpkError("KTRF body checksum mismatch")
    sections = {}
    sections_n = struct.unpack_from("<I", blob, 16)[0]
    if struct.unpack_from("<Q", blob, 24)[0] != 96:
        raise LocaleGpkError("KTRF section directory position invalid")
    for i in range(sections_n):
        at = 96 + 48*i
        name = blob[at:at+4].decode("ascii")
        offset, size, decoded = struct.unpack_from("<QQQ", blob, at+8)
        count = struct.unpack_from("<I", blob, at+32)[0]
        if offset + size > len(blob) or decoded != size or name in sections:
            raise LocaleGpkError("Invalid KTRF section")
        sections[name] = (blob[offset:offset+size], count)
    strings, count = sections["STRS"]
    string_count, byte_count = struct.unpack_from("<II", strings)
    offsets = struct.unpack_from("<" + str(string_count+1) + "I", strings, 8)
    text = strings[12 + 4*string_count:]
    if len(text) != byte_count or offsets[-1] != byte_count or count != string_count:
        raise LocaleGpkError("Invalid KTRF STRS string table")
    values = [text[offsets[i]:offsets[i+1]].decode("utf-8")
              for i in range(string_count)]
    nodes, count = sections["NODE"]
    node_count, size, _, _, start, _ = struct.unpack_from("<6I", nodes)
    if (node_count != EXPECTED_KTRF_NODES or count != node_count or
            size != 32 or start != 24):
        raise LocaleGpkError("Unexpected KTRF NODE shape")
    result = {}
    for node in range(node_count):
        at = 24 + node*32
        id_index, kind_index = struct.unpack_from("<II", nodes, at)
        if values[kind_index] != "ktrf:scene":
            continue
        tag = values[id_index]
        if not tag.startswith("sdhq:node:"):
            raise LocaleGpkError("Invalid KTRF scene ID")
        key = ("english/" + tag[len("sdhq:node:"):] + ".eng.ors").lower()
        if key in result:
            raise LocaleGpkError("Ambiguous KTRF scene path")
        result[key] = node
    if len(result) != EXPECTED_SCENE_NODES:
        raise LocaleGpkError("KTRF scene NODE inventory changed")
    return result


def tick(value: bytes) -> int | None:
    match = TICK_RE.fullmatch(value)
    if match is None:
        return None
    m, s, frame = (int(v) for v in match.groups())
    # The native sscanf tick reader intentionally does not require s<60,
    # frame<24. Source authored out-of-range timestamps are preserved bytewise.
    return ((m * 60) + s) * 24 + frame


@dataclass(frozen=True)
class Event:
    kind: str
    index: int
    start: int
    end: int
    fields: tuple[bytes, ...]


class NativeOrs:
    """Reproduce the native Ors.cpp parser's event order, including UNKNOWN."""

    def __init__(self, data: bytes):
        self.lines = data.split(b"\n")
        self.events = []
        for index, raw in enumerate(self.lines):
            line = raw[:-1] if raw.endswith(b"\r") else raw
            if not line.startswith(b"["):
                continue
            close = line.find(b"]", 1)
            if close < 0:
                continue
            try:
                kind = line[1:close].decode("ascii")
            except UnicodeError:
                kind = "UNKNOWN"
            if kind not in FIELD_COUNTS:
                # Native parser creates an UNKNOWN event with startTick=0.
                self.events.append(Event("UNKNOWN", index, 0, 0, tuple()))
                continue
            equal, end = line.find(b"="), line.rfind(b";")
            if equal < 0 or end < equal:
                continue
            fields = tuple(line[equal+1:end].split(b"\t"))
            if len(fields) != FIELD_COUNTS[kind]:
                continue  # Native parser discards malformed known commands.
            first = tick(fields[0])
            last = tick(fields[-1]) if len(fields) > 1 else first
            if first is None or last is None:
                continue
            self.events.append(Event(kind, index, first, last, fields))
        self.events.sort(key=lambda e: (e.start, e.index))

    def printed_slots(self):
        for ordinal, event in enumerate(self.events):
            if event.kind == "PrintText":
                if event.fields[2]:
                    yield ordinal, 0, event, 2, event.fields[2]
            elif event.kind == "SetSELECT":
                # Native SetSELECT options filter null/empty before numbering.
                index = 0
                for field_index in (1, 2):
                    value = event.fields[field_index]
                    if value in (b"", b"null", b"NULL", b'""'):
                        continue
                    yield ordinal, index, event, field_index, value
                    index += 1

    def put_field(self, event: Event, field_index: int, value: str) -> None:
        if any(v in value for v in ("\t", "\0", "\r", "\n")):
            raise LocaleGpkError("Unsafe ORS translation control characters")
        data = value.encode("utf-8")
        if not data or len(data) > MAX_TRANSLATION_BYTES:
            raise LocaleGpkError("ORS translation size or content is invalid")
        old = self.lines[event.index]
        ending = b"\r" if old.endswith(b"\r") else b""
        body = old[:-1] if ending else old
        prefix = b"[" + event.kind.encode() + b"]="
        if not body.startswith(prefix) or not body.endswith(b";"):
            raise LocaleGpkError("ORS event changed during field replacement")
        # IMPORTANT: read from the already patched line. SetSELECT can have
        # two translated option slots on the SAME event; reusing immutable
        # event.fields would overwrite the first translated option on slot 1.
        fields = body[len(prefix):-1].split(b"\t")
        if len(fields) != len(event.fields):
            raise LocaleGpkError("ORS field count changed during translation")
        fields[field_index] = data
        self.lines[event.index] = (
            b"[" + event.kind.encode() + b"]=" + b"\t".join(fields) +
            b";" + ending
        )

    def finish(self) -> bytes:
        value = b"\n".join(self.lines)
        if [(e.kind, e.start, e.end) for e in NativeOrs(value).events] != [
                (e.kind, e.start, e.end) for e in self.events]:
            raise LocaleGpkError("Native ORS event order/timing drift after translation")
        return value


class LegacySpanish:
    """Read one named ORS from legacy NSIS GPK; never execute any installer."""

    def __init__(self, path: Path):
        path = verify_file(path)
        if digest_file_stream(path) != LEGACY_SPANISH_HASH:
            raise LocaleGpkError("Spanish HQ NSIS GPK SHA-256 unexpected")
        blob = path.read_bytes()
        if blob[-32:-20] != b"STKFile0PIDX" or blob[-16:] != b"STKFile0PACKFILE":
            raise LocaleGpkError("Spanish legacy GPK signature mismatch")
        size = struct.unpack_from("<I", blob, len(blob)-20)[0]
        key = bytes.fromhex("82 ee 1d b3 57 e9 2c c2 2f 54 7b 10 4c 9a 75 49")
        coded = blob[-32-size:-32]
        decoded = bytes(v ^ key[i % 16] for i, v in enumerate(coded))
        index = zlib.decompress(decoded[4:])
        if len(index) != int.from_bytes(decoded[:4], "little"):
            raise LocaleGpkError("Spanish legacy GPK PIDX length mismatch")
        at = 0
        entries = {}
        while at < len(index):
            units = struct.unpack_from("<H", index, at)[0]
            at += 2
            if units == 0 or at + units*2 + 23 > len(index):
                raise LocaleGpkError("Malformed Spanish legacy PIDX")
            name = index[at:at+units*2].decode("utf-16le").lower()
            at += units*2
            sub, ver, zero, offset, stored, magic, orig, prefix = (
                struct.unpack_from("<HHHII4sIB", index, at))
            at += 23
            start = index[at:at+prefix]
            at += prefix
            if sub != 11 or ver != 1 or zero or magic != b"DFLT":
                raise LocaleGpkError("Unsupported Spanish legacy member layout")
            if name in entries:
                raise LocaleGpkError("Duplicate Spanish legacy member")
            entries[name] = (offset, stored, orig, start)
        if len(entries) != EXPECTED_RETAIL_ENTRIES-2:
            raise LocaleGpkError("Spanish legacy GPK ORS count changed")
        self.blob = blob
        self.entries = entries

    def read_one(self, logical: str) -> bytes:
        offset, stored, original, prefix = self.entries[logical.lower()]
        raw = prefix + self.blob[offset:offset+stored-len(prefix)]
        body = zlib.decompress(raw)
        if len(body) != original:
            raise LocaleGpkError("Legacy Spanish ORS size mismatch")
        return body


def normalize_line_breaks(value: str) -> tuple[str, bool]:
    # ORS is line-delimited. Native HQ source already uses the *two bytes*
    # '\\n' to encode a linebreak inside the PrintText field.
    if "\0" in value or "\t" in value:
        raise LocaleGpkError("Translation contains forbidden control characters")
    changed = "\n" in value or "\r" in value
    normalized = value.replace("\r\n", "\n").replace("\r", "\n").replace("\n", r"\n")
    return normalized, changed


def reviewed_identity(section: str, contextual: str, original: str,
                      manifest: dict) -> bool:
    reasons = manifest.get(section, {})
    if not isinstance(reasons, dict):
        raise LocaleGpkError("Invalid reviewed identity section")
    reason = reasons.get(contextual, reasons.get(original, ""))
    if not isinstance(reason, str):
        return False
    simplified = "".join(reason.lower().split())
    return simplified in APPROVED_IDENTITY_REASONS


def translate_scene(data: bytes, node: int, dictionary: dict,
                    reviews: dict, stats: Counter) -> bytes:
    section = str(node)
    translations = dictionary.get(section)
    if not isinstance(translations, dict):
        raise LocaleGpkError(f"Missing Spanish translation for NODE {node}")
    ors = NativeOrs(data)
    for ordinal, slot, event, field, raw in ors.printed_slots():
        try:
            original = raw.decode("utf-8")
        except UnicodeError as exc:
            raise LocaleGpkError(f"Non-UTF8 retail text NODE {node}") from exc
        context = f"@ors:{ordinal}:{slot}"
        value = translations.get(context, translations.get(original))
        if not isinstance(value, str) or not value:
            raise LocaleGpkError(
                f"Missing/blank Spanish text NODE {node} {context}")
        if value == original:
            if not reviewed_identity(section, context, original, reviews):
                raise LocaleGpkError(
                    f"Unreviewed source-identity NODE {node} {context}")
            stats["reviewedIdentity"] += 1
        value, escaped = normalize_line_breaks(value)
        if escaped:
            stats["escapedNewlines"] += 1
        if context in translations:
            stats["contextual"] += 1
        stats["choices" if event.kind == "SetSELECT" else "printText"] += 1
        ors.put_field(event, field, value)
    return ors.finish()


def normalize_unmapped(english: bytes, spanish: LegacySpanish) -> bytes:
    # KTRF has two spare ORS paths, one has no dialogue. The other has exactly
    # one Japanese PrintText. Authenticate its Spanish translation using the
    # original authored tick and speaker. No positional guess/EN fallback.
    original = NativeOrs(english)
    slots = list(original.printed_slots())
    if len(slots) != 1:
        raise LocaleGpkError("Unexpected unreferenced retail ORS text inventory")
    ord_, slot, event, field, text = slots[0]
    if (event.kind != "PrintText" or slot != 0 or event.start != 3626 or
            event.fields[1] != b"Makoto"):
        raise LocaleGpkError("Unreferenced Japanese retail line changed")
    candidates = []
    patched = NativeOrs(spanish.read_one(LEGACY_UNMAPPED_SCENE))
    for _, _, other, _, candidate in patched.printed_slots():
        if (other.kind == "PrintText" and other.start == event.start and
                other.fields[1] == event.fields[1] and candidate):
            candidates.append(candidate)
    if len(candidates) != 1:
        raise LocaleGpkError("No unique Spanish equivalent for unreferenced Japanese line")
    translated = candidates[0].decode("utf-8")
    if translated == text.decode("utf-8"):
        raise LocaleGpkError("Japanese text remained untranslated")
    original.put_field(event, field, translated)
    return original.finish()


def source_member_bytes(reader: GpkReader, name: str) -> bytes:
    # The two retail .DS_STORE entries have a noncanonical original_length=0.
    # Read them only in memory; they are not semantically translated and the
    # derived archive encodes them as standard raw entries (length consistent).
    entry = reader.entries[name]
    if entry.magic != b"    " or entry.original_length != 0:
        raise LocaleGpkError("Unexpected retail metadata member format")
    with reader.path.open("rb") as stream:
        stream.seek(entry.offset)
        body = stream.read(entry.stored_length-len(entry.prefix))
    if len(body) != entry.stored_length-len(entry.prefix):
        raise LocaleGpkError("Truncated retail metadata member")
    return entry.prefix + body


def build(retail: Path, legacy_hq: Path, translations: Path, reviews: Path,
          route: Path, *, publish: bool = False) -> tuple[dict[str, bytes], Counter]:
    retail = verify_file(retail)
    if (retail.name.casefold() != "script.gpk" or
            retail.parent.name.casefold() != "packs" or
            digest_file_stream(retail) != RETAIL_ENGLISH_SHA256):
        raise LocaleGpkError("Only original retail Packs/Script.GPK is allowed")
    reader = GpkReader(retail)
    scene_nodes = read_ktrf_scenes(route)
    spanish = LegacySpanish(legacy_hq)
    if (len(reader.entries) != EXPECTED_RETAIL_ENTRIES or
            set(spanish.entries) != set(k for k in reader.entries if k.endswith(".ors"))):
        raise LocaleGpkError("Spanish/retail GPK paths differ")
    dictionary, reviewed = read_json(translations), read_json(reviews)
    if not isinstance(dictionary.get("nodo"), dict) or dictionary["nodo"]:
        raise LocaleGpkError("Spanish JSON global fallback is unexpected")
    if set(dictionary) != {str(n) for n in scene_nodes.values()} | {"nodo"}:
        raise LocaleGpkError("Spanish JSON NODE inventory differs from KTRF")
    files = {}
    stats = Counter()
    for name in reader.entries:
        exact_name = reader.original_names[name]
        if not name.endswith(".ors"):
            files[exact_name] = source_member_bytes(reader, name)
            continue
        original = reader.read_ors(name)
        if name in scene_nodes:
            translated = translate_scene(
                original, scene_nodes[name], dictionary, reviewed, stats)
        elif name == LEGACY_UNMAPPED_SCENE:
            translated = normalize_unmapped(original, spanish)
            stats["unmappedLocalized"] += 1
        elif name == OTHER_UNMAPPED_SCENE:
            if list(NativeOrs(original).printed_slots()):
                raise LocaleGpkError("Unmapped routing ORS unexpectedly has dialogue")
            translated = original
            stats["unmappedNoDialogue"] += 1
        else:
            raise LocaleGpkError(f"Unknown non-KTRF retail script: {name}")
        files[exact_name] = translated
        a = NativeOrs(original)
        b = NativeOrs(translated)
        if [(x.kind, x.start, x.end) for x in a.events] != [
                (x.kind, x.start, x.end) for x in b.events]:
            raise LocaleGpkError(f"Retimed or reordered retail ORS {name}")
        for old, new in zip(a.events, b.events):
            if old.kind not in ("PrintText", "SetSELECT"):
                if a.lines[old.index] != b.lines[new.index]:
                    raise LocaleGpkError(f"Retail non-text command changed: {name}")
        stats["scenes"] += 1
    if (len(files) != EXPECTED_RETAIL_ENTRIES or
            stats["scenes"] != EXPECTED_RETAIL_ENTRIES-2 or
            stats["printText"] != EXPECTED_TEXT or
            stats["choices"] != EXPECTED_CHOICES or
            stats["contextual"] != EXPECTED_CONTEXT or
            stats["escapedNewlines"] != EXPECTED_NL_ESCAPES or
            stats["unmappedLocalized"] != 1 or stats["unmappedNoDialogue"] != 1):
        raise LocaleGpkError(f"Spanish 100% coverage census failed: {stats}")
    return files, stats


def write_qa_output(files: dict[str, bytes], stats: Counter) -> Path:
    base = STATE_ROOT.resolve(strict=True)
    out = base / "spanish-normalized" / ("run-" + uuid.uuid4().hex)
    out.mkdir(parents=True, exist_ok=False)
    temp = out / ".unpublished-Spanish.GPK"
    final = out / "Script - Spanish.GPK"
    try:
        result = write_full_archive(temp, files)
        reader = GpkReader(temp)
        if len(reader.entries) != EXPECTED_RETAIL_ENTRIES:
            raise LocaleGpkError("Persisted Spanish GPK inventory mismatch")
        for name, expected in files.items():
            if reader.read_ors(name) != expected:
                raise LocaleGpkError("Persisted Spanish member roundtrip mismatch")
        temp.rename(final)
        with (out / "audit.txt").open("x", encoding="utf-8") as text:
            text.write("PRIVATE SPANISH HQ GPK — NOT INSTALLED\n")
            text.write(f"SHA-256: {result['sha256'].upper()}\n")
            text.write(f"Bytes: {result['bytes']}\n")
            text.write(f"Entries: {len(files)}\n")
            for key, count in sorted(stats.items()):
                text.write(f"{key}: {count}\n")
            text.write("Source GPKs, retail/Release/saves left unchanged.\n")
        return final
    finally:
        temp.unlink(missing_ok=True)


def write_full_archive(path: Path, files: dict[str, bytes]) -> dict:
    """Write canonical raw/seekable 1859-member pack; never overwrite."""
    if (len(files) != EXPECTED_RETAIL_ENTRIES or path.suffix.lower() != ".gpk"
            or not path.resolve().is_relative_to(STATE_ROOT.resolve())):
        raise LocaleGpkError("Unsafe normalized Spanish output location/inventory")
    index = bytearray()
    expected = []
    with path.open("xb") as stream:
        for logical, data in files.items():
            off = stream.tell()
            if not data or off + len(data) > 0xffffffff:
                raise LocaleGpkError("GPK member empty or 32-bit offset overflow")
            stream.write(data)
            index.extend(write_index_entry(logical, off, len(data)))
            expected.append({
                "path": logical, "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            })
        index.extend(b"\0\0\0\0")
        compressed = zlib.compress(index, 9)
        coded = struct.pack("<I", len(index)) + compressed
        encoded = bytes(v ^ KEY[i % 16] for i, v in enumerate(coded))
        stream.write(encoded)
        stream.write(
            TRAILER_MARKER + struct.pack("<I", len(encoded)) + PACK_MARKER)
    # Distinct parser path verifies all persisted members and PIDX ordering.
    verify_archive(path, expected)
    return {"sha256": digest_file_stream(path), "bytes": path.stat().st_size}


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--retail-gpk", type=Path, required=True)
    p.add_argument("--legacy-spanish-gpk", type=Path, required=True)
    p.add_argument("--es-json", type=Path, required=True)
    p.add_argument("--es-reviewed", type=Path, required=True)
    p.add_argument("--ktrf", type=Path, required=True)
    p.add_argument("--build", action="store_true",
                   help="otherwise dry-run only; writes only to QA state")
    a = p.parse_args(argv)
    try:
        files, stats = build(a.retail_gpk, a.legacy_spanish_gpk,
                             a.es_json, a.es_reviewed, a.ktrf)
        print("ES_RETAIL_NORMALIZATION_PREFLIGHT_PASS", dict(stats))
        if a.build:
            print("PRIVATE_SPANISH_GPK", write_qa_output(files, stats))
        else:
            print("DRY_RUN_ONLY; no GPK written. Use --build after reviewing.")
        return 0
    except (ValueError, UnicodeError, OSError, KeyError) as exc:
        print("ES_RETAIL_NORMALIZATION_BLOCKED:", exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
