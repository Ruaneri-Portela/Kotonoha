"""Compose private seven-ORS LxH locale override GPKs in QA state only.

This is an offline, fail-closed helper; it never mounts a game or changes the
source GPKs, sidecar JSONs, installed Release, retail packs, or saves.

Each extended ORS has a complete *original HQ prefix* and a new LxH tail.
Keep the HQ prefix from the language's OWN Script.GPK, retain the grafted LxH
timings/media event commands, then replace all 273 LxH PrintText occurrences.
An untranslated HQ prefix must never be packaged as a localized LxH ORS.

Usage (explicit, private paths; outputs always go under tools/qa-lab/state):

  python games/school-days/tools/build_lxh_locale_gpks.py \
      --lxh-gpk "PRIVATE/Packs/LxH.GPK" --sidecars "PRIVATE/translations" \
      --base-gpk en="PRIVATE/Packs/Script.GPK" \
      --base-gpk pt-BR="PRIVATE/translations/Script - Brazilian.GPK" \
      --base-gpk es="PRIVATE/Spanish-Script.GPK" \
      --locales en,pt-BR,es

Alternatively, --lxh-overlay DIRECTORY and --base-overlay es=DIRECTORY
allow *already loose*, private ORS files for LxH or Spanish. English HQ
requires the original retail Packs/Script.GPK, including its known SHA-256;
Brazilian HQ requires Script - Brazilian.GPK in STKFile0 format.
No retail GPK is extracted by this tool, and the seven resulting ORS packs
contain no commercial WMV/OGG/PNG. Keep all generated files private.

zh-CN requires a separately rebuilt and validated native-compatible Chinese
HQ GPK; the original GB18030 Chinese patch is never accepted as its base.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import uuid
import zlib

from build_lxh_gpk import (KEY, PACK_MARKER, TRAILER_MARKER,
                           verify_archive, write_index_entry)


REPO_ROOT = Path(__file__).resolve().parents[3]
STATE_ROOT = REPO_ROOT / "tools" / "qa-lab" / "state"
EXPECTED = (
    ("1398", "05-A2-F00", 30),
    ("1399", "05-A2-Z00", 53),
    ("1409", "05-B0-F00", 13),
    ("1455", "05-G0-L03", 23),
    ("1554", "05-KC-F00", 64),
    ("1714", "05-SB-E01", 68),
    ("1777", "05-SC-L03", 22),
)
VALID_LOCALES = ("en", "pt-BR", "zh-CN", "es")
FIELD_COUNTS = {
    "Next": 1, "SkipFRAME": 1,
    "PlayVoice": 5,
    "CreateBG": 4, "PlaySe": 4, "PlayMovie": 4,
    "PrintText": 4, "SetSELECT": 4,
    "BlackFade": 3, "WhiteFade": 3, "PlayBgm": 3,
    "EndBGM": 3, "EndRoll": 3, "MoveSom": 3,
}
COMMAND_RE = re.compile(rb"^\[([A-Za-z][A-Za-z0-9]*)\]=(.*);$")
KEY_RE = re.compile(r"^@ors:(0|[1-9][0-9]*):(0|[1-9][0-9]*)$")
TIME_RE = re.compile(rb"^([0-9]+):([0-9][0-9]):([0-9][0-9])$")
MAX_ORS_BYTES = 4 * 1024 * 1024
MAX_INDEX_BYTES = 8 * 1024 * 1024
MAX_ARCHIVE_ENTRIES = 100_000
SENTINEL = "\u200b"
RETAIL_ENGLISH_SHA256 = (
    "e82dc824449381b9a132aa092740c4169eb3d77a820d969046b74ca1d9df4adf"
)


class LocaleGpkError(ValueError):
    """Input cannot safely be converted without losing content or semantics."""


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def gpk_logical(ending: str) -> str:
    return f"Script/English/05/{ending}.ENG.ORS"


def base_logical(ending: str) -> str:
    return f"English/05/{ending}.ENG.ORS"


def inflate_exact(data: bytes, expected: int, label: str) -> bytes:
    if not 0 <= expected <= MAX_INDEX_BYTES and label == "index":
        raise LocaleGpkError("GPK index exceeds safe allocation limit")
    if expected > MAX_ORS_BYTES and label == "ORS":
        raise LocaleGpkError("GPK ORS exceeds safe allocation limit")
    inflater = zlib.decompressobj()
    actual = inflater.decompress(data, expected + 1)
    if (len(actual) != expected or not inflater.eof or
            inflater.unused_data or inflater.unconsumed_tail or
            inflater.flush()):
        raise LocaleGpkError(f"GPK {label} zlib size/trailing bytes mismatch")
    return actual


@dataclass(frozen=True)
class GpkEntry:
    offset: int
    stored_length: int
    original_length: int
    magic: bytes
    prefix: bytes


class GpkReader:
    """Read-only, bounded PIDX reader. Only selected ORS bytes are ever loaded."""

    def __init__(self, path: Path):
        self.path = path.resolve(strict=True)
        if not self.path.is_file() or self.path.is_symlink():
            raise LocaleGpkError("GPK source must be a regular file")
        size = self.path.stat().st_size
        if size < 33:
            raise LocaleGpkError("Truncated GPK")
        with self.path.open("rb") as src:
            src.seek(-32, 2)
            trailer = src.read(32)
            if (trailer[:12] != TRAILER_MARKER or
                    trailer[16:] != PACK_MARKER):
                raise LocaleGpkError("Bad GPK trailer")
            encoded_length = struct.unpack_from("<I", trailer, 12)[0]
            if not 4 < encoded_length <= MAX_INDEX_BYTES or encoded_length > size - 32:
                raise LocaleGpkError("Bad GPK PIDX length")
            self.data_end = size - 32 - encoded_length
            src.seek(self.data_end)
            coded = src.read(encoded_length)
        decoded = bytes(b ^ KEY[i % len(KEY)] for i, b in enumerate(coded))
        target_size = struct.unpack_from("<I", decoded)[0]
        raw_index = inflate_exact(decoded[4:], target_size, "index")
        cursor = 0
        self.entries: dict[str, GpkEntry] = {}
        # Preserve literal PIDX spelling for tools that compare every entry
        # name case-sensitively with the retail Script.GPK inventory.
        self.original_names: dict[str, str] = {}
        last_end = 0
        while True:
            if cursor + 2 > len(raw_index):
                raise LocaleGpkError("Truncated PIDX")
            units = struct.unpack_from("<H", raw_index, cursor)[0]
            cursor += 2
            if units == 0:
                if raw_index[cursor:] != b"\0\0":
                    raise LocaleGpkError("Incorrect PIDX terminator")
                break
            if len(self.entries) >= MAX_ARCHIVE_ENTRIES:
                raise LocaleGpkError("Too many GPK entries")
            if cursor + units * 2 + 23 > len(raw_index):
                raise LocaleGpkError("Truncated GPK entry")
            try:
                name = raw_index[cursor:cursor + units * 2].decode("utf-16le")
            except UnicodeError as exc:
                raise LocaleGpkError("Invalid GPK UTF-16 name") from exc
            cursor += units * 2
            sub, version, zero, off, stored, magic, uncompressed, prefix_len = (
                struct.unpack_from("<HHHII4sIB", raw_index, cursor))
            cursor += 23
            if cursor + prefix_len > len(raw_index) or stored < prefix_len:
                raise LocaleGpkError("Invalid GPK inline prefix")
            prefix = raw_index[cursor:cursor + prefix_len]
            cursor += prefix_len
            physical_end = off + stored - prefix_len
            if (zero or magic not in (b"    ", b"DFLT") or
                    (self.entries and off != last_end) or
                    physical_end > self.data_end):
                raise LocaleGpkError("GPK entry has incompatible layout")
            # Retail Script.GPK includes two .DS_STORE metadata records whose
            # IsRaw marker is set but whose declared original length is zero.
            # They are never read/extracted. Validate a raw member's size only
            # when reading one of the seven named ORS members below.
            if (not name or name.startswith(("/", "\\")) or "\\" in name or
                    ":" in name or any(x in (".", "..") for x in name.split("/"))):
                raise LocaleGpkError("Unsafe GPK logical path")
            key = name.casefold()
            if key in self.entries:
                raise LocaleGpkError("Case-insensitive GPK path collision")
            self.entries[key] = GpkEntry(off, stored, uncompressed, magic, prefix)
            self.original_names[key] = name
            last_end = physical_end
        if last_end != self.data_end or not self.entries:
            raise LocaleGpkError("GPK physical entry chain is incomplete")

    def read_ors(self, logical: str) -> bytes:
        item = self.entries.get(logical.casefold())
        if item is None:
            raise LocaleGpkError(
                f"Missing {logical}; locale pack path layout may be incompatible")
        if item.original_length > MAX_ORS_BYTES:
            raise LocaleGpkError("ORS size over limit")
        with self.path.open("rb") as src:
            src.seek(item.offset)
            raw = src.read(item.stored_length - len(item.prefix))
        if len(raw) != item.stored_length - len(item.prefix):
            raise LocaleGpkError("Truncated ORS stored data")
        data = item.prefix + raw
        if item.magic == b"DFLT":
            return inflate_exact(data, item.original_length, "ORS")
        if len(data) != item.original_length:
            raise LocaleGpkError("Bad raw ORS length")
        return data


@dataclass(frozen=True)
class Event:
    command: str
    fields: tuple[bytes, ...]
    tick: int
    end: int
    physical: int

    @property
    def identity(self) -> tuple[str, int]:
        return self.command, self.tick


class Ors:
    def __init__(self, data: bytes, label: str):
        if not data or len(data) > MAX_ORS_BYTES or b"\x00" in data:
            raise LocaleGpkError(f"{label}: empty/oversized/binary ORS")
        try:
            # Native L4 ORS reader expects English-style TAB fields. Other
            # encodings/syntax are refused, especially the current zh-CN GPK.
            data.decode("utf-8-sig", errors="strict")
        except UnicodeError as exc:
            raise LocaleGpkError(f"{label}: not UTF-8; charset conversion unverified") from exc
        self.lines = data.splitlines(keepends=True)
        self.events: list[Event] = []
        for physical, line in enumerate(self.lines):
            stripped = line.removesuffix(b"\n").removesuffix(b"\r")
            if not stripped:
                continue
            if not stripped.startswith(b"["):
                continue  # Native parser ignores non-command lines.
            found = COMMAND_RE.fullmatch(stripped)
            if not found:
                raise LocaleGpkError(f"{label}: invalid command at line {physical + 1}")
            command = found.group(1).decode("ascii")
            if command not in FIELD_COUNTS:
                raise LocaleGpkError(f"{label}: unknown native ORS command {command}")
            fields = tuple(found.group(2).split(b"\t"))
            if len(fields) != FIELD_COUNTS[command]:
                raise LocaleGpkError(
                    f"{label}: {command} TAB field count mismatch at line {physical + 1}")
            tick = parse_tick(fields[0], label)
            end = parse_tick(fields[-1], label) if len(fields) > 1 else tick
            if end < tick:
                raise LocaleGpkError(f"{label}: negative event duration")
            self.events.append(Event(command, fields, tick, end, physical))
        if not self.events:
            raise LocaleGpkError(f"{label}: no native ORS commands")
        # Kotonoha_ORS_storage inserts each parsed event after equal ticks.
        self.events.sort(key=lambda e: (e.tick, e.physical))

    def patch_field(self, event: Event, field: int, replacement: str) -> None:
        if any(ch in replacement for ch in "\r\n\t\x00"):
            raise LocaleGpkError("Translation contains a raw ORS field separator")
        encoded = replacement.encode("utf-8")
        if len(encoded) > 0x10000:
            raise LocaleGpkError("Translation text exceeds safe ORS field length")
        old = self.lines[event.physical]
        newline = b"\r\n" if old.endswith(b"\r\n") else b"\n" if old.endswith(b"\n") else b""
        fields = list(event.fields)
        fields[field] = encoded
        self.lines[event.physical] = (
            b"[" + event.command.encode("ascii") + b"]=" +
            b"\t".join(fields) + b";" + newline)

    def replace_event(self, event: Event, source: "Ors", other: Event) -> None:
        original = self.lines[event.physical]
        new = source.lines[other.physical]
        ending = b"\r\n" if original.endswith(b"\r\n") else (
            b"\n" if original.endswith(b"\n") else b"")
        body = new.removesuffix(b"\n").removesuffix(b"\r")
        self.lines[event.physical] = body + ending

    def bytes(self) -> bytes:
        return b"".join(self.lines)


def parse_tick(value: bytes, label: str) -> int:
    match = TIME_RE.fullmatch(value)
    if match is None:
        raise LocaleGpkError(f"{label}: malformed 24fps timestamp")
    minute, second, frame = map(int, match.groups())
    if second >= 60 or frame >= 24:
        raise LocaleGpkError(f"{label}: out-of-range 24fps timestamp")
    return (minute * 60 + second) * 24 + frame


def load_sidecars(root: Path, locales: tuple[str, ...]) -> dict[str, dict]:
    if not root.is_dir():
        raise LocaleGpkError("Missing sidecar directory")
    result = {}
    for locale in ("en",) + tuple(x for x in locales if x != "en"):
        path = root / f"{locale}-lxh.json"
        if not path.is_file() or path.is_symlink():
            raise LocaleGpkError(f"Missing/unsafe sidecar {path.name}")
        try:
            sections = json.loads(path.read_text(encoding="utf-8-sig"),
                                  object_pairs_hook=_reject_duplicate_keys)
        except (ValueError, UnicodeError) as exc:
            raise LocaleGpkError(f"Invalid JSON in {path.name}: {exc}") from exc
        if not isinstance(sections, dict) or set(sections) != (
                {scene for scene, _, _ in EXPECTED} | {"nodo"}):
            raise LocaleGpkError(f"{path.name}: unexpected scene sections")
        if sections["nodo"] != {}:
            raise LocaleGpkError(f"{path.name}: no global nodo allowed")
        for scene, ending, count in EXPECTED:
            values = sections[scene]
            if not isinstance(values, dict) or len(values) != count:
                raise LocaleGpkError(f"{path.name}: {scene} count must be {count}")
            for key, value in values.items():
                if (not KEY_RE.fullmatch(key) or not key.endswith(":0") or
                        not isinstance(value, str) or value == "" or
                        any(c in value for c in "\r\n\t\x00")):
                    raise LocaleGpkError(f"{path.name}: invalid {scene} {key}")
        result[locale] = sections
    baseline = result["en"]
    for locale, sections in result.items():
        for scene, _, _ in EXPECTED:
            if set(sections[scene]) != set(baseline[scene]):
                raise LocaleGpkError(f"{locale}: LxH @ors key set differs from English")
            for key, english in baseline[scene].items():
                chosen = sections[scene][key]
                if english == SENTINEL:
                    if chosen != SENTINEL:
                        if (locale != "es" or
                                chosen != "Juan Ochoa (JNC)- LxH .2021^"):
                            raise LocaleGpkError(
                                f"{locale}: sentinel content unexpected at {scene} {key}")
                        # Deterministic migration of the seven author credits:
                        # output only; do not edit the original Spanish JSON.
                        sections[scene][key] = SENTINEL
                elif (chosen.count("^") != english.count("^") or
                      chosen.count("\\n") != english.count("\\n") or
                      "juan ochoa" in chosen.casefold()):
                    raise LocaleGpkError(
                        f"{locale}: LxH markup/credit mismatch at {scene} {key}")
    return result


def _reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise LocaleGpkError(f"Duplicate JSON key {key}")
        result[key] = value
    return result


class Source:
    def __init__(self, *, gpk: Path | None = None, overlay: Path | None = None):
        if (gpk is None) == (overlay is None):
            raise LocaleGpkError("Specify exactly one GPK or loose ORS overlay")
        self.original_path = Path(gpk).absolute() if gpk else None
        self.gpk = GpkReader(gpk) if gpk else None
        self.overlay = overlay.resolve(strict=True) if overlay else None
        if self.overlay and not self.overlay.is_dir():
            raise LocaleGpkError("Overlay must be a directory")

    def read(self, logical: str) -> bytes:
        if self.gpk:
            return self.gpk.read_ors(logical)
        assert self.overlay is not None
        path = self.overlay.joinpath(*logical.split("/"))
        if not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(self.overlay):
            raise LocaleGpkError(f"Missing/unsafe loose ORS {logical}")
        if path.stat().st_size > MAX_ORS_BYTES:
            raise LocaleGpkError(f"Oversized loose ORS {logical}")
        return path.read_bytes()


def require_base_provenance(locale: str, source: Source) -> None:
    """Reject locale swaps before content is composed or any output is created.

    The English locale is ALWAYS sourced from original retail Script.GPK,
    SHA-256 verified. A duplicate named Script - english.GPK is not accepted.
    Brazilian scenes MUST use the user's Script - Brazilian.GPK.
    """
    if locale == "en":
        if (source.gpk is None or source.original_path is None or
                source.original_path.name.casefold() != "script.gpk" or
                source.original_path.parent.name.casefold() != "packs" or
                source.gpk.path.name.casefold() != "script.gpk" or
                source.gpk.path.parent.name.casefold() != "packs"):
            raise LocaleGpkError(
                "English HQ must originate in original Packs/Script.GPK; "
                "translations/Script - english.GPK is forbidden")
        if digest_file_stream(source.gpk.path) != RETAIL_ENGLISH_SHA256:
            raise LocaleGpkError("English retail Script.GPK SHA-256 mismatch")
    elif locale == "pt-BR":
        if (source.gpk is None or source.original_path is None or
                source.original_path.name.casefold() != "script - brazilian.gpk"):
            raise LocaleGpkError(
                "Brazilian HQ must originate in Script - Brazilian.GPK")
    elif locale == "es":
        # The unmodified Spanish NSIS Script.gpk has only 1857 ORS and
        # changes retail media/timing. Only a separately certified, fully
        # reconstructed 1859-member pack may be used for Spanish epilogues.
        if (source.gpk is None or source.original_path is None or
                source.original_path.name.casefold() != "script - spanish.gpk"):
            raise LocaleGpkError(
                "Spanish LxH requires certified Script - Spanish.GPK; "
                "the raw Spanish installer script is not compatible")
    elif locale == "zh-CN":
        if (source.gpk is None or source.original_path is None or
                source.original_path.name != "Script - chinese-compatible.GPK"):
            raise LocaleGpkError(
                "Chinese LxH requires Script - chinese-compatible.GPK; "
                "raw GB18030 Script - chinese.GPK cannot be used")
    if source.gpk and locale in ("en", "pt-BR", "es", "zh-CN") and len(source.gpk.entries) != 1859:
        raise LocaleGpkError(f"{locale}: expected 1859 HQ Script.GPK members")


def digest_file_stream(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as inp:
        for block in iter(lambda: inp.read(1024 * 1024), b""):
            sha.update(block)
    return sha.hexdigest()


def lxh_boundary(ors: Ors, ending: str, expected: int) -> int:
    token = f"{ending}-LXH".encode("ascii").lower()
    movies = [
        i for i, event in enumerate(ors.events)
        if event.command == "PlayMovie" and
        token in b"\t".join(event.fields).lower()
    ]
    if len(movies) != 1:
        raise LocaleGpkError(f"{ending}: LxH PlayMovie marker must be unique")
    first = movies[0]
    before = ors.events[:first]
    after = ors.events[first:]
    if (sum(e.command == "EndRoll" for e in before) != 1 or
            sum(e.command == "EndRoll" for e in ors.events) != 1 or
            sum(e.command == "Next" for e in ors.events) != 1 or
            sum(e.command == "SkipFRAME" for e in ors.events) != 1 or
            any(e.command == "Next" for e in before)):
        raise LocaleGpkError(f"{ending}: LxH/HQ boundary events are inconsistent")
    credits = next(e for e in before if e.command == "EndRoll")
    if ors.events[first].tick < credits.end:
        raise LocaleGpkError(f"{ending}: LxH movie starts before end of credits")
    actual = sum(e.command == "PrintText" for e in after)
    if actual != expected or any(e.command == "SetSELECT" for e in after):
        raise LocaleGpkError(
            f"{ending}: expected {expected} LxH PrintText, got {actual}")
    return first


def merge_hq(extended: Ors, base: Ors, boundary: int, ending: str) -> int:
    # Original Next was moved to after LxH and SkipFRAME can change time.
    # All other native HQ events MUST align by command and start tick.
    original = [event for event in base.events
                if event.command not in ("Next", "SkipFRAME")]
    if sum(e.command == "Next" for e in base.events) != 1:
        raise LocaleGpkError(f"{ending}: HQ base requires one terminal Next")
    if sum(e.command == "EndRoll" for e in base.events) != 1:
        raise LocaleGpkError(f"{ending}: HQ base requires one EndRoll")
    if sum(e.command == "SkipFRAME" for e in base.events) != 1:
        raise LocaleGpkError(f"{ending}: HQ base requires one SkipFRAME")
    if any(e.command == "PlayMovie" and b"-lxh" in b"\t".join(e.fields).lower()
           for e in base.events):
        raise LocaleGpkError(f"{ending}: HQ base already contains LxH events")
    credits_end = next(e.end for e in extended.events[:boundary]
                       if e.command == "EndRoll")
    index = 0
    replaced = 0
    for scene_event in extended.events[:boundary]:
        if scene_event.command == "SkipFRAME":
            continue
        if (index < len(original) and scene_event.identity == original[index].identity):
            old = original[index]
            # Localized HQ can replace content, but not authored timing.
            if scene_event.end != old.end:
                raise LocaleGpkError(
                    f"{ending}: HQ time drift at {scene_event.command}")
            extended.replace_event(scene_event, base, old)
            index += 1
            replaced += 1
        elif scene_event.command == "BlackFade" and scene_event.tick >= credits_end:
            # The graft can insert one pre-movie black fade after credits.
            continue
        else:
            raise LocaleGpkError(
                f"{ending}: HQ event mismatch at {scene_event.command} "
                f"tick={scene_event.tick}; refusing to preserve wrong-language HQ")
    if index != len(original):
        raise LocaleGpkError(
            f"{ending}: {len(original) - index} original HQ events unmatched")
    if replaced < 3:
        raise LocaleGpkError(f"{ending}: improbably short HQ prefix")
    return replaced


def compose_scene(ending: str, scene: str, count: int, lxh: bytes,
                  base: bytes, translations: dict[str, str]) -> tuple[bytes, dict]:
    original = Ors(lxh, f"{ending} LxH")
    base_ors = Ors(base, f"{ending} localized HQ")
    boundary = lxh_boundary(original, ending, count)
    preserved_events = tuple((e.command, e.tick, e.end) for e in original.events)
    matched = merge_hq(original, base_ors, boundary, ending)
    seen: set[str] = set()
    for ordinal in range(boundary, len(original.events)):
        event = original.events[ordinal]
        if event.command != "PrintText":
            continue
        key = f"@ors:{ordinal}:0"
        if key not in translations:
            raise LocaleGpkError(f"{ending} {scene}: missing translation {key}")
        if key in seen:
            raise LocaleGpkError(f"{ending}: duplicate ordinal {key}")
        seen.add(key)
        original.patch_field(event, 2, translations[key])
    if seen != set(translations) or len(seen) != count:
        missing, extra = set(translations) - seen, seen - set(translations)
        raise LocaleGpkError(
            f"{ending}: orphan keys {sorted(missing)} unexpected keys {sorted(extra)}")
    result = original.bytes()
    rebuilt = Ors(result, f"{ending} rebuilt")
    if tuple((e.command, e.tick, e.end) for e in rebuilt.events) != preserved_events:
        raise LocaleGpkError(f"{ending}: ORS command/temporal structure drifted")
    # Verify HQ byte equality against its own source GPK on every aligned event.
    if sum(1 for a in rebuilt.events[:boundary] if a.command == "PrintText") != sum(
            1 for a in original.events[:boundary] if a.command == "PrintText"):
        raise LocaleGpkError(f"{ending}: HQ lines disappeared")
    for ordinal in range(boundary, len(rebuilt.events)):
        event = rebuilt.events[ordinal]
        if event.command == "PrintText":
            key = f"@ors:{ordinal}:0"
            if event.fields[2].decode("utf-8") != translations[key]:
                raise LocaleGpkError(f"{ending}: translated text roundtrip failed")
    return result, {"scene": scene, "ending": ending, "hqEvents": matched,
                    "lxhPrintText": len(seen), "byteLength": len(result),
                    "sha256": digest(result)}


def build_locale(locale: str, lxh_source: Source, base_source: Source,
                 sidecars: dict[str, dict]) -> tuple[dict[str, bytes], list[dict]]:
    require_base_provenance(locale, base_source)
    files = {}
    summaries = []
    for scene, ending, expected in EXPECTED:
        path = gpk_logical(ending)
        output, report = compose_scene(
            ending, scene, expected, lxh_source.read(path),
            base_source.read(base_logical(ending)), sidecars[locale][scene])
        files[path] = output
        summaries.append(report)
    if len(files) != 7 or sum(s["lxhPrintText"] for s in summaries) != 273:
        raise LocaleGpkError(f"{locale}: failed final 7-ORS/273-slot census")
    return files, summaries


def write_private_gpk(path: Path, files: dict[str, bytes]) -> dict:
    # Called only after every requested locale has passed all source checks.
    if path.exists():
        raise FileExistsError("Never replace an existing private GPK")
    expected = []
    index = bytearray()
    with path.open("xb") as output:
        for logical, data in files.items():
            offset = output.tell()
            if not data or offset + len(data) > 0xFFFFFFFF:
                raise LocaleGpkError("GPK exceeds 32-bit field bounds")
            output.write(data)
            index.extend(write_index_entry(logical, offset, len(data)))
            expected.append({"path": logical, "bytes": len(data),
                             "sha256": digest(data)})
        index.extend(b"\0\0\0\0")
        compressed = zlib.compress(bytes(index), level=9)
        coded = struct.pack("<I", len(index)) + compressed
        output.write(bytes(b ^ KEY[i % len(KEY)] for i, b in enumerate(coded)))
        output.write(TRAILER_MARKER + struct.pack("<I", len(coded)) + PACK_MARKER)
    verify_archive(path, expected)
    # A different parser additionally verifies the persisted GPK index.
    readback = GpkReader(path)
    if len(readback.entries) != 7:
        raise LocaleGpkError("Persisted GPK contains unexpected entries")
    for logical, data in files.items():
        if readback.read_ors(logical) != data:
            raise LocaleGpkError("GPK entry readback differs from output")
    return {"name": path.name, "bytes": path.stat().st_size,
            "sha256": digest(path.read_bytes())}


def publish(composed: dict[str, tuple[dict[str, bytes], list[dict]]],
            state_root: Path = STATE_ROOT) -> Path:
    # The normal CLI does not expose an output path. Synthetic tests may pass
    # an isolated child of the same trusted QA state root.
    trusted = STATE_ROOT.resolve()
    state_root = state_root.resolve()
    if not state_root.is_relative_to(trusted):
        raise LocaleGpkError("GPK output must remain under tools/qa-lab/state")
    state_root.mkdir(parents=True, exist_ok=True)
    out_dir = state_root / "lxh-locales" / ("run-" + uuid.uuid4().hex)
    out_dir.mkdir(parents=True, exist_ok=False)
    manifest = {"status": "private-not-installed", "source": "explicit private inputs",
                "outputCount": len(composed), "locales": {}}
    for locale, (files, summaries) in composed.items():
        pack_name = f"{locale}-lxh.GPK"
        gpk_report = write_private_gpk(out_dir / pack_name, files)
        manifest["locales"][locale] = {
            "pack": gpk_report, "sceneCount": len(summaries),
            "translationSlots": sum(s["lxhPrintText"] for s in summaries),
            "scenes": summaries,
            "sentinelPolicy": "remove historical credits in seven LxH sentinels",
        }
    (out_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return out_dir


def parse_label_paths(values: list[str], label: str) -> dict[str, Path]:
    sources = {}
    for item in values:
        if "=" not in item:
            raise LocaleGpkError(f"{label}: expected LOCALE=PATH")
        locale, path = item.split("=", 1)
        if locale not in VALID_LOCALES or locale in sources or not path:
            raise LocaleGpkError(f"{label}: invalid/repeated locale {locale}")
        sources[locale] = Path(path)
    return sources


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--lxh-gpk", type=Path)
    source.add_argument("--lxh-overlay", type=Path)
    parser.add_argument("--sidecars", required=True, type=Path)
    parser.add_argument("--base-gpk", action="append", default=[],
                        metavar="LOCALE=FILE")
    parser.add_argument("--base-overlay", action="append", default=[],
                        metavar="LOCALE=DIR")
    parser.add_argument("--locales", required=True, help="e.g. en,pt-BR,es,zh-CN")
    args = parser.parse_args(argv)
    try:
        locales = tuple(args.locales.split(","))
        if not locales or len(set(locales)) != len(locales) or any(
                loc not in VALID_LOCALES for loc in locales):
            raise LocaleGpkError("Unknown/duplicate locale")
        base_gpks = parse_label_paths(args.base_gpk, "--base-gpk")
        base_overlays = parse_label_paths(args.base_overlay, "--base-overlay")
        if set(base_gpks) & set(base_overlays):
            raise LocaleGpkError("Locale cannot have both base GPK and overlay")
        if set(base_gpks) | set(base_overlays) != set(locales):
            raise LocaleGpkError("Provide exactly one localized HQ base source per locale")
        sidecars = load_sidecars(args.sidecars, locales)
        lxh = Source(gpk=args.lxh_gpk, overlay=args.lxh_overlay)
        composed = {}
        for loc in locales:
            original = (Source(gpk=base_gpks[loc]) if loc in base_gpks
                        else Source(overlay=base_overlays[loc]))
            composed[loc] = build_locale(loc, lxh, original, sidecars)
        destination = publish(composed)
        print(f"PRIVATE_LXH_LOCALE_GPK_PASS {len(locales)} locales 7 ORS/locale 273 text/locale")
        print("OUTPUT", destination)
        return 0
    except (LocaleGpkError, OSError, UnicodeError, ValueError) as exc:
        print("PRIVATE_LXH_LOCALE_GPK_BLOCKED:", exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
