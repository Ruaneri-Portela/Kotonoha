"""Audit/convert the original Chinese School Days Script.GPK, fail closed.

The Chinese patch is a distinct STKFile0 archive: raw ORS entries declare
original_length=0, use GB18030, quoted comma-delimited commands, and have
different scene names/event timelines from retail. Neither decoding GB18030
nor changing delimiters proves compatibility with the strict Windows L4 mount.

Example (never modifies either input):

  python games/school-days/tools/convert_chinese_script_gpk.py \
      --chinese "C:/Users/Suzu/Downloads/traduções/Script - chinese.GPK" \
      --retail "build/school-days-dev-control4/games/school-days/Release/Packs/Script.GPK" \
      --audit-only

All generated reports and (only if EVERY audit succeeds) compatible GPKs are
confined to tools/qa-lab/state. No English translation GPK is read or written.
On any malformed Chinese event, missing/extra scene, nontext asset/timing drift,
or unprovable speaker mapping, no derived archive will be produced.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import uuid
import zlib

from build_lxh_gpk import KEY, PACK_MARKER, TRAILER_MARKER, verify_archive, write_index_entry
from build_lxh_locale_gpks import (
    FIELD_COUNTS, GpkReader, LocaleGpkError, RETAIL_ENGLISH_SHA256,
    STATE_ROOT, digest_file_stream, inflate_exact,
)


class ChineseCompatibilityError(LocaleGpkError):
    """Unsafe to write, or impossible to prove native mount parity."""


# Mappings are intentionally explicit. Guessing unrecognized speakers, e.g.
# assigning an unknown name to a different character, is not localization.
SPEAKERS = {
    "诚": "Makoto", "誠": "Makoto", "世界": "Sekai", "言叶": "Kotonoha",
    "泰介": "Taisuke", "刹那": "Setsuna", "光": "Hikari",
    "七海": "Nanami", "乙女": "Otome", "夏美": "Natsumi",
    "南": "Minami",
}

QUOTED_COMMAND = re.compile(r'^\[([A-Za-z][A-Za-z0-9]*)\]="(.*)";$')
CHINESE_ORIGINAL = re.compile(r"^([0-9]{2})/([A-Za-z0-9-]+)\.ors$", re.I)
# C++ lib/src/parsers/Ors.cpp:66-76 parses source ticks using sscanf and
# computes 24 * (60 * minutes + seconds) + frames WITHOUT range validation.
# Retail itself contains frame components >=24. Do not "fix" or reject those
# timestamps merely because an offline helper expects canonical 0..23.
NATIVE_TIME = re.compile(r"^[0-9]+:[0-9]{2}:[0-9]{2}$")
NATIVE_COMMAND = re.compile(rb"^\[([A-Za-z][A-Za-z0-9]*)\]=(.*);$")
MAX_ISSUE_EXAMPLES = 40
MAX_SOURCE_SIZE = 4 * 1024 * 1024


def in_state(path: Path) -> Path:
    """Resolve output destination without allowing outside QA state."""
    state = STATE_ROOT.resolve()
    target = path.resolve()
    if not target.is_relative_to(state) or target == state:
        raise ChineseCompatibilityError(
            "Output must remain under tools/qa-lab/state")
    return target


def require_original_retail(reader: GpkReader) -> None:
    path = reader.path
    if path.name.casefold() != "script.gpk" or path.parent.name.casefold() != "packs":
        raise ChineseCompatibilityError("English HQ MUST come from original Packs/Script.GPK")
    if digest_file_stream(path) != RETAIL_ENGLISH_SHA256:
        raise ChineseCompatibilityError("Retail Script.GPK SHA-256 does not match original")
    if len(reader.entries) != 1859:
        raise ChineseCompatibilityError("Retail Script.GPK must have 1859 entries")


def read_raw_chinese(reader: GpkReader, key: str) -> bytes:
    """Read legacy Chinese IsRaw/original_length=0 entries, strictly bounded."""
    item = reader.entries[key.casefold()]
    if (item.magic != b"    " or item.original_length not in (0, item.stored_length) or
            item.stored_length <= 0 or item.stored_length > MAX_SOURCE_SIZE):
        raise ChineseCompatibilityError(f"{key}: unsupported Chinese raw entry layout")
    with reader.path.open("rb") as source:
        source.seek(item.offset)
        payload = source.read(item.stored_length - len(item.prefix))
    if len(payload) != item.stored_length - len(item.prefix):
        raise ChineseCompatibilityError(f"{key}: truncated raw entry")
    return item.prefix + payload


def source_name_to_retail(name: str) -> str:
    matched = CHINESE_ORIGINAL.fullmatch(name)
    if not matched:
        raise ChineseCompatibilityError(f"Unsafe or unknown Chinese ORS name: {name}")
    return f"English/{matched.group(1)}/{matched.group(2)}.ENG.ORS"


def retail_original_names(reader: GpkReader) -> list[str]:
    """Recover exact index capitalization rather than using folded map keys.

    Mount validation compares the full PIDX path set byte-for-byte (UTF-16), so
    `English/00/...` must never be accidentally repacked in lowercase.
    """
    with reader.path.open("rb") as source:
        source.seek(reader.data_end)
        encoded = source.read(reader.path.stat().st_size - reader.data_end - 32)
    coded = bytes(value ^ KEY[i % len(KEY)] for i, value in enumerate(encoded))
    length = struct.unpack_from("<I", coded)[0]
    raw = inflate_exact(coded[4:], length, "index")
    names = []
    cursor = 0
    while True:
        count = struct.unpack_from("<H", raw, cursor)[0]
        cursor += 2
        if not count:
            if raw[cursor:] != b"\0\0":
                raise ChineseCompatibilityError("Bad trailing index terminator")
            break
        name = raw[cursor:cursor + count * 2].decode("utf-16le", "strict")
        cursor += count * 2
        prefix_length = raw[cursor + 22]
        cursor += 23 + prefix_length
        names.append(name)
    if len(names) != len(reader.entries) or {n.casefold() for n in names} != set(reader.entries):
        raise ChineseCompatibilityError("Original retail index casefold mismatch")
    return names


def convert_line(line: str, location: str,
                 *, unknown_speakers: set[str] | None = None) -> str:
    """Convert one *complete* Chinese command, without changing its timing.

    PrintText is right-split at the final timestamp so commas inside dialogue
    survive exactly. We never silently insert an absent field or a speaker.
    """
    match = QUOTED_COMMAND.fullmatch(line)
    if not match:
        raise ChineseCompatibilityError(f"{location}: malformed/unterminated Chinese command")
    command, payload = match.groups()
    count = FIELD_COUNTS.get(command)
    if count is None:
        raise ChineseCompatibilityError(f"{location}: unsupported Chinese command {command}")
    if command == "PrintText":
        head = payload.split(", ", 2)
        if len(head) != 3 or ", " not in head[2]:
            raise ChineseCompatibilityError(f"{location}: PrintText missing speaker/text/end field")
        text, ending = head[2].rsplit(", ", 1)
        fields = [head[0], head[1], text, ending]
        if not text:
            raise ChineseCompatibilityError(f"{location}: empty Chinese dialogue")
        if fields[1] in SPEAKERS:
            fields[1] = SPEAKERS[fields[1]]
        elif unknown_speakers is None:
            raise ChineseCompatibilityError(f"{location}: unmapped Chinese speaker label")
        else:
            # *Audit only*: keep the original label intact so the remaining
            # event/choice structure can still be compared and reported. The
            # unreconciled speaker remains an explicit blocking issue.
            unknown_speakers.add(fields[1])
    else:
        fields = payload.split(", ")
    if len(fields) != count or any("\r" in f or "\n" in f or "\t" in f or "\0" in f for f in fields):
        raise ChineseCompatibilityError(f"{location}: native TAB fields cannot preserve source payload")
    if not NATIVE_TIME.fullmatch(fields[0]) or (count > 1 and not NATIVE_TIME.fullmatch(fields[-1])):
        raise ChineseCompatibilityError(f"{location}: invalid 24fps timing in Chinese script")
    if count > 1:
        a = fields[0].split(":")
        b = fields[-1].split(":")
        if (int(b[0]) * 60 + int(b[1])) * 24 + int(b[2]) < (
                int(a[0]) * 60 + int(a[1])) * 24 + int(a[2]):
            raise ChineseCompatibilityError(f"{location}: negative command duration")
    return f"[{command}]=" + "\t".join(fields) + ";"


def convert_scene(source: bytes, label: str,
                  *, unknown_speakers: set[str] | None = None) -> bytes:
    if not source or b"\0" in source or len(source) > MAX_SOURCE_SIZE:
        raise ChineseCompatibilityError(f"{label}: empty/binary/oversized Chinese ORS")
    try:
        decoded = source.decode("gb18030", "strict")
    except UnicodeError as exc:
        raise ChineseCompatibilityError(f"{label}: invalid GB18030 source") from exc
    if decoded.encode("gb18030", "strict") != source:
        raise ChineseCompatibilityError(f"{label}: GB18030 roundtrip not exact")
    converted = []
    for index, line in enumerate(decoded.splitlines(), start=1):
        if not line.strip():
            converted.append("")
        elif line.strip() == ";":
            # Standalone trailing scene sentinel, not an executable command.
            converted.append(";")
        else:
            converted.append(convert_line(
                line, f"{label}:{index}", unknown_speakers=unknown_speakers))
    encoded = ("\r\n".join(converted) + "\r\n").encode("utf-8")
    # This is the real native parser's expected field grammar; a generated
    # script with a dropped command must not reach the pack writer.
    native = parse_native(encoded, label)
    source_commands = sum(bool(QUOTED_COMMAND.fullmatch(line)) for line in decoded.splitlines())
    if source_commands != len(native):
        raise ChineseCompatibilityError(f"{label}: converted native event count drift")
    return encoded


@dataclass(frozen=True)
class NativeEvent:
    command: str
    fields: tuple[bytes, ...]
    tick: int
    end: int
    physical: int


def parse_native_tick(text: bytes, label: str) -> int:
    try:
        value = text.decode("ascii", "strict")
    except UnicodeError as exc:
        raise ChineseCompatibilityError(f"{label}: timestamp is not ASCII") from exc
    if not NATIVE_TIME.fullmatch(value):
        raise ChineseCompatibilityError(f"{label}: malformed native timestamp")
    minute, second, frame = map(int, value.split(":"))
    return (minute * 60 + second) * 24 + frame


def parse_native(source: bytes, label: str) -> list[NativeEvent]:
    """Strict native TAB grammar with *actual* C++ timestamp arithmetic.

    The installed C++ parser ignores blank and standalone ';' lines, sorts
    events by startTick with source-file order as a stable tie break, and has
    no frame<=23 guard. This auditor does not silently drop malformed commands.
    """
    try:
        source.decode("utf-8-sig", "strict")
    except UnicodeError as exc:
        raise ChineseCompatibilityError(f"{label}: translated ORS not UTF-8") from exc
    events = []
    for index, source_line in enumerate(source.splitlines(), start=1):
        line = source_line.strip()
        if not line or line == b";" or line.startswith(b"#"):
            continue
        match = NATIVE_COMMAND.fullmatch(line)
        if not match:
            raise ChineseCompatibilityError(f"{label}:{index}: malformed native ORS command")
        command = match.group(1).decode("ascii")
        count = FIELD_COUNTS.get(command)
        if count is None:
            raise ChineseCompatibilityError(f"{label}:{index}: unsupported native command")
        fields = tuple(match.group(2).split(b"\t"))
        if len(fields) != count or any(b"\0" in f for f in fields):
            raise ChineseCompatibilityError(f"{label}:{index}: native field arity differs")
        tick = parse_native_tick(fields[0], label)
        end = parse_native_tick(fields[-1], label) if count > 1 else tick
        if end < tick:
            raise ChineseCompatibilityError(f"{label}:{index}: command has negative duration")
        events.append(NativeEvent(command, fields, tick, end, index))
    if not events:
        raise ChineseCompatibilityError(f"{label}: native ORS has no events")
    events.sort(key=lambda event: (event.tick, event.physical))
    return events


def compare_scene(chinese: bytes, original: bytes, label: str) -> list[str]:
    a, b = parse_native(chinese, label), parse_native(original, label + " retail")
    issues = []
    if len(a) != len(b):
        issues.append(f"event-count {len(a)} vs retail {len(b)}")
    a_cmd = [x.command for x in a]
    b_cmd = [x.command for x in b]
    if a_cmd != b_cmd:
        issues.append("command-order mismatch")
    for cmd in ("Next", "SkipFRAME", "SetSELECT", "PlayMovie", "PlayVoice", "PrintText"):
        ca, cb = a_cmd.count(cmd), b_cmd.count(cmd)
        if ca != cb:
            issues.append(f"{cmd}-count {ca} vs retail {cb}")
    if a_cmd == b_cmd:
        if any(x.tick != y.tick or x.end != y.end for x, y in zip(a, b)):
            issues.append("command timing differs from retail")
        for x, y in zip(a, b):
            if x.command == "SetSELECT":
                nonnull = lambda e: sum(v.lower() not in (b"null", b'""', b"")
                                        for v in e.fields[1:-1])
                if nonnull(x) != nonnull(y):
                    issues.append("choice-option structure differs from retail")
                    break
            elif x.command not in ("PrintText", "SetSELECT") and x.fields[1:-1] != y.fields[1:-1]:
                issues.append("nontext media/control arguments differ from retail")
                break
    return issues


def issue_category(reason: str) -> str:
    for fragment, category in (
        ("extra scene", "extra-scene"), ("missing Chinese scene", "missing-scene"),
        ("event-count", "event-count-drift"), ("command-order", "command-order-drift"),
        ("-count", "event-kind-count-drift"), ("timing differs", "timing-drift"),
        ("choice-option", "choice-structure-drift"),
        ("media/control arguments", "nontext-payload-drift"),
        ("unmapped Chinese speaker", "unmapped-speaker"),
        ("malformed/unterminated", "malformed-command"),
        ("invalid 24fps", "invalid-timestamp"),
        ("negative command duration", "invalid-timestamp"),
        ("native TAB fields", "unrepresentable-fields"),
        ("missing speaker/text/end", "missing-dialogue-field"),
        ("GB18030", "encoding-error"),
        ("raw entry layout", "archive-entry-format"),
        ("scene total", "index-count-mismatch"),
    ):
        if fragment in reason:
            return category
    return "other-unsafe-source"


def line_census(data: bytes) -> Counter[str]:
    """Independent command-line census, including scenes that fail parsing.

    It deliberately does not treat dialogue as aligned or make any semantic
    assumptions; it can still count `[PrintText]` in a malformed source.
    """
    return Counter(match.group(1).decode("ascii") for match in
                   re.finditer(rb"(?m)^\[([A-Za-z][A-Za-z0-9]*)\]=", data))


@dataclass
class Audit:
    report: dict
    files: dict[str, bytes]


def audit(chinese: GpkReader, retail: GpkReader,
          *, verify_provenance: bool = True) -> Audit:
    if verify_provenance:
        require_original_retail(retail)
    issues: Counter[str] = Counter()
    examples: list[dict] = []
    transformed: dict[str, bytes] = {}
    expected = {k for k in retail.entries if k.endswith(".eng.ors")}
    actual = set()
    successes = 0
    matched_census = 0
    source_census: Counter[str] = Counter()
    retail_census: Counter[str] = Counter()
    printtext_census_mismatch = 0
    select_census_mismatch = 0

    def record(scene: str, reason: str) -> None:
        issues[issue_category(reason)] += 1
        if len(examples) < MAX_ISSUE_EXAMPLES:
            examples.append({"scene": scene, "issue": reason})

    for name in chinese.entries:
        try:
            target = source_name_to_retail(name)
            key = target.casefold()
            if key in actual:
                raise ChineseCompatibilityError("Duplicate normalized Chinese scene")
            actual.add(key)
            if key not in expected:
                record(name, "extra scene absent from original retail index")
                continue
            source_bytes = read_raw_chinese(chinese, name)
            reference = retail.read_ors(key)
            raw_counts = line_census(source_bytes)
            retail_counts = line_census(reference)
            source_census.update(raw_counts)
            retail_census.update(retail_counts)
            matched_census += 1
            if raw_counts["PrintText"] != retail_counts["PrintText"]:
                printtext_census_mismatch += 1
            if raw_counts["SetSELECT"] != retail_counts["SetSELECT"]:
                select_census_mismatch += 1
            unknown_speakers: set[str] = set()
            converted = convert_scene(source_bytes, name,
                                      unknown_speakers=unknown_speakers)
            errors = compare_scene(converted, reference, name)
            if unknown_speakers:
                record(name, f"unmapped Chinese speaker labels ({len(unknown_speakers)} unique)")
            if errors:
                for item in errors:
                    record(name, item)
            elif not unknown_speakers:
                transformed[key] = converted
                successes += 1
        except (LocaleGpkError, UnicodeError, ValueError, OSError) as exc:
            record(name, str(exc).split(";", 1)[0])
    for missing in sorted(expected - actual):
        record(missing, "missing Chinese scene")
    metadata = sorted(k for k in retail.entries if not k.endswith(".eng.ors"))
    if len(metadata) != 2 or metadata != [".ds_store", "english/.ds_store"]:
        record("retail", "Unexpected metadata members in original retail")
    if len(actual) != len(expected):
        record("index", f"Chinese scene total {len(actual)} differs from retail {len(expected)}")
    summary = {
        "status": "blocked" if issues else "compatible-not-installed",
        "format": "GB18030 Chinese quoted comma ORS -> native UTF-8 TAB",
        "reference": "original Packs/Script.GPK only",
        "sourceEntries": len(chinese.entries),
        "retailEntries": len(retail.entries),
        "chineseScenes": len(actual),
        "retailScenes": len(expected),
        "fullyCompatibleScenes": successes,
        "independentLineCensus": {
            "comparableScenes": matched_census,
            "chineseCommands": dict(sorted(source_census.items())),
            "retailCommands": dict(sorted(retail_census.items())),
            "printTextCountMismatchScenes": printtext_census_mismatch,
            "setSELECTCountMismatchScenes": select_census_mismatch,
            "warning": "Line-anchored command counts only; not proof of semantic alignment",
        },
        "issueCounts": dict(sorted(issues.items())),
        "issueExamples": examples,
        "note": "Converting syntax cannot repair Chinese script/control/timing drift; no English fallback",
    }
    return Audit(summary, transformed if not issues else {})


def write_compatible_gpk(path: Path, files: dict[str, bytes], retail: GpkReader) -> None:
    path = in_state(path)
    if path.exists():
        raise FileExistsError(f"Refusing existing output {path}")
    original_names = retail_original_names(retail)
    if set(x.casefold() for x in files) != {
            n.casefold() for n in original_names if n.casefold().endswith(".eng.ors")}:
        raise ChineseCompatibilityError("Incomplete translated corpus: cannot create GPK")
    path.parent.mkdir(parents=True, exist_ok=True)
    expected = []
    index = bytearray()
    created = False
    try:
        with path.open("xb") as output:
            created = True
            for name in original_names:
                body = (files[name.casefold()] if name.casefold() in files
                        else read_raw_chinese(retail, name))
                if not body or len(body) > MAX_SOURCE_SIZE:
                    raise ChineseCompatibilityError(f"{name}: invalid output member")
                offset = output.tell()
                output.write(body)
                index.extend(write_index_entry(name, offset, len(body)))
                expected.append({"path": name, "bytes": len(body),
                                 "sha256": hashlib.sha256(body).hexdigest()})
            index.extend(b"\0\0\0\0")
            coded = struct.pack("<I", len(index)) + zlib.compress(bytes(index), level=9)
            encoded = bytes(b ^ KEY[i % len(KEY)] for i, b in enumerate(coded))
            output.write(encoded)
            output.write(TRAILER_MARKER + struct.pack("<I", len(encoded)) + PACK_MARKER)
        verify_archive(path, expected)
        verified = GpkReader(path)
        if len(verified.entries) != len(retail.entries):
            raise ChineseCompatibilityError("Readback GPK entry-count mismatch")
        for name, body in files.items():
            if verified.read_ors(name) != body:
                raise ChineseCompatibilityError(f"{name}: readback bytes differ")
    except BaseException:
        if created:
            path.unlink(missing_ok=True)
        raise


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chinese", required=True, type=Path, help="Original Chinese patch GPK; read-only")
    parser.add_argument("--retail", required=True, type=Path, help="Original Packs/Script.GPK; read-only")
    parser.add_argument("--audit-only", action="store_true", help="Never generate an output GPK")
    parser.add_argument("--output", type=Path, default=STATE_ROOT / "Script - chinese-compatible.GPK")
    parser.add_argument("--report", type=Path, help="New JSON report under QA state (never overwrite)")
    args = parser.parse_args(argv)
    try:
        destination = in_state(args.output)
        if destination.name != "Script - chinese-compatible.GPK":
            raise ChineseCompatibilityError("Output must be named Script - chinese-compatible.GPK")
        report_path = in_state(args.report) if args.report else in_state(
            STATE_ROOT / "chinese-gpk-audit" / ("run-" + uuid.uuid4().hex) / "report.json")
        if report_path.exists() or report_path == destination:
            raise FileExistsError("Refusing to overwrite pre-existing audit report")
        chinese = GpkReader(args.chinese)
        retail = GpkReader(args.retail)
        require_original_retail(retail)
        result = audit(chinese, retail)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        with report_path.open("x", encoding="utf-8") as file:
            json.dump(result.report, file, indent=2, ensure_ascii=False)
            file.write("\n")
        print("CHINESE_GPK_AUDIT", result.report["status"], "report", report_path)
        print("COMPATIBLE_SCENES", result.report["fullyCompatibleScenes"], "/",
              result.report["retailScenes"])
        print("ISSUES", json.dumps(result.report["issueCounts"], ensure_ascii=False))
        if result.report["status"] == "blocked":
            print("CHINESE_GPK_BLOCKED: source must not be mounted as compatible", file=sys.stderr)
            return 2
        if args.audit_only:
            return 0
        write_compatible_gpk(destination, result.files, retail)
        print("PRIVATE_CHINESE_COMPATIBLE_GPK", destination)
        return 0
    except (ChineseCompatibilityError, LocaleGpkError, ValueError, UnicodeError,
            OSError, FileExistsError) as exc:
        print("CHINESE_GPK_BLOCKED:", exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
