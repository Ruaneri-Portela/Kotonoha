#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import sys
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path

FORMAT = "school-days-ors-event-audit-v1"
VERSION = "1.0.0"
HDR = re.compile(r"^\s*\[([^\]]+)\]")
TS = re.compile(r"^\d+:\d{1,2}:\d{1,2}$")

# Exact headers currently recognized by src/parsers/Ors.c.
PARSER = {
    "CreateBG": "CREATE_BG",
    "PlaySe": "PLAY_SE",
    "PlayMovie": "PLAY_MOVIE",
    "WhiteFade": "WHITE_FADE",
    "BlackFade": "BLACK_FADE",
    "PlayBgm": "PLAY_BGM",
    "PrintText": "PRINT_TEXT",
    "PlayVoice": "PLAY_VOICE",
    "SkipFRAME": "SkipFRAME",
    "SetSELECT": "SetSELECT",
    "EndBGM": "END_BGM",
    "EndRoll": "END_ROLL",
    "Next": "Next",
}

# Source-level baseline at the start of G1.
# "validated" means there is an in-game behavior gate, not merely code.
BASE = {
    "CreateBG": (
        "EventManager",
        "partial",
        "BG is registered; first payload field is parsed but ignored; lifetime/composition semantics need validation.",
    ),
    "PlaySe": (
        "EventManager",
        "partial",
        "SE is scheduled; numeric payload is parsed but ignored; overlap/stop semantics need validation.",
    ),
    "PlayMovie": (
        "EventManager",
        "partial",
        "Movie is scheduled with 24-fps mapping; numeric payload is parsed but ignored; variants need validation.",
    ),
    "WhiteFade": (
        "EventManager/Fade",
        "validated",
        "ORS IN/OUT overlay is implemented and visually validated.",
    ),
    "BlackFade": (
        "EventManager/Fade",
        "validated",
        "ORS IN/OUT overlay is implemented and visually validated.",
    ),
    "PlayBgm": (
        "EventManager",
        "partial",
        "BGM is scheduled using _LOOP asset convention; replace/loop semantics need validation.",
    ),
    "PrintText": (
        "Event constructor/libass",
        "partial",
        "Timed subtitles are created; original style/lifetime behavior needs validation.",
    ),
    "PlayVoice": (
        "EventManager",
        "partial",
        "Voice + A/B/C image cycling exists; numeric payload is ignored; mouth/portrait semantics are not closed.",
    ),
    "SkipFRAME": (
        "Event constructor",
        "boundary-only",
        "Currently only contributes to Event::lastTime.",
    ),
    "SetSELECT": (
        "Event constructor/Prompt + KTRF",
        "partial",
        "Prompt + KTRF choice commit exist; timeline blocking/timing still need validation.",
    ),
    "EndBGM": (
        "EventManager",
        "suspect",
        "Current code schedules an audio asset instead of an explicit stop/fade operation; requires focused research.",
    ),
    "EndRoll": (
        "EventManager/Video",
        "partial",
        "Currently registers video; end-title/credits handoff semantics need validation.",
    ),
    "Next": (
        "Event constructor",
        "boundary-only",
        "Currently only contributes to Event::lastTime; KTRF advances after ORS end.",
    ),
}


def decode(data: bytes):
    for encoding in ("utf-8-sig", "utf-8", "cp932", "cp1252"):
        try:
            return data.decode(encoding), encoding
        except UnicodeDecodeError:
            pass
    return data.decode("latin-1"), "latin-1-fallback"


def sample(value: str, limit: int = 180) -> str:
    value = value.replace("\r", "\\r").replace("\n", "\\n")
    return value if len(value) <= limit else value[: limit - 3] + "..."


def parse_line(line: str):
    match = HDR.match(line)
    if not match:
        return None
    command = match.group(1)
    if "=" not in line:
        return command, []
    rhs = line.split("=", 1)[1].split(";", 1)[0]
    return command, rhs.split("\t")


def iter_ors_files(root: Path):
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in {".git", "build", "build-win", ".vs"}]
        for name in files:
            if name.lower().endswith(".ors"):
                yield Path(base) / name


def audit(root: Path, limit: int):
    files = sorted(iter_ors_files(root), key=lambda p: str(p).lower())
    counts = Counter()
    command_files = defaultdict(set)
    field_counts = defaultdict(Counter)
    examples = defaultdict(list)
    unknown = Counter()
    encodings = Counter()
    malformed = []
    rows = []
    per_file = []
    aggregate = hashlib.sha256()

    for path in files:
        data = path.read_bytes()
        text, encoding = decode(data)
        encodings[encoding] += 1
        rel = path.relative_to(root).as_posix()
        sha = hashlib.sha256(data).hexdigest()
        aggregate.update((rel + "\0" + sha + "\n").encode())

        local = Counter()
        event_lines = 0
        for line_number, raw in enumerate(text.splitlines(), 1):
            parsed = parse_line(raw)
            if parsed is None:
                continue

            command, fields = parsed
            event_lines += 1
            counts[command] += 1
            local[command] += 1
            command_files[command].add(rel)
            after_start = fields[1:] if fields else []
            field_counts[command][len(after_start)] += 1

            enum_name = PARSER.get(command, "UNKNOWN")
            if enum_name == "UNKNOWN":
                unknown[command] += 1

            if "=" not in raw or not fields:
                malformed.append(
                    {
                        "file": rel,
                        "line": line_number,
                        "command": command,
                        "reason": "missing = or payload",
                        "raw": sample(raw),
                    }
                )
            elif not TS.match(fields[0].strip()):
                malformed.append(
                    {
                        "file": rel,
                        "line": line_number,
                        "command": command,
                        "reason": "start is not HH:MM:FF-like",
                        "raw": sample(raw),
                    }
                )

            rows.append((command, enum_name, rel, line_number, len(after_start), after_start, raw))
            if len(examples[command]) < limit:
                examples[command].append(
                    {
                        "file": rel,
                        "line": line_number,
                        "field_count_after_start": len(after_start),
                        "fields": [sample(x, 120) for x in after_start],
                        "raw": sample(raw),
                    }
                )

        per_file.append(
            {
                "path": rel,
                "sha256": sha,
                "encoding": encoding,
                "event_lines": event_lines,
                "commands": dict(sorted(local.items())),
            }
        )

    commands = []
    for command in sorted(counts, key=lambda c: (-counts[c], c.lower())):
        where, status, notes = BASE.get(
            command,
            (
                "none",
                "unsupported",
                "Header exists in ORS but is not an exact recognized parser command.",
            ),
        )
        commands.append(
            {
                "command": command,
                "parser_enum": PARSER.get(command, "UNKNOWN"),
                "count": counts[command],
                "files": len(command_files[command]),
                "field_counts_after_start": {
                    str(k): v for k, v in sorted(field_counts[command].items())
                },
                "runtime_where": where,
                "runtime_status": status,
                "notes": notes,
                "examples": examples[command],
            }
        )

    report = {
        "format": FORMAT,
        "tool_version": VERSION,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "root": str(root.resolve()),
        "corpus": {
            "ors_files": len(files),
            "event_lines": sum(counts.values()),
            "aggregate_manifest_sha256": aggregate.hexdigest(),
            "encodings": dict(sorted(encodings.items())),
        },
        "commands": commands,
        "unknown_headers": [
            {"command": k, "count": v}
            for k, v in sorted(unknown.items(), key=lambda item: (-item[1], item[0]))
        ],
        "malformed_event_lines": malformed,
        "files": per_file,
    }
    return report, rows


def write_outputs(report, rows, out_dir: Path):
    out_dir.mkdir(parents=True, exist_ok=True)

    with (out_dir / "event_occurrences.csv").open(
        "w", encoding="utf-8-sig", newline=""
    ) as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "command",
                "parser_enum",
                "file",
                "line",
                "field_count_after_start",
                "fields_json",
                "raw",
            ]
        )
        for command, enum_name, rel, line_number, count, fields, raw in rows:
            writer.writerow(
                [
                    command,
                    enum_name,
                    rel,
                    line_number,
                    count,
                    json.dumps(fields, ensure_ascii=False),
                    raw,
                ]
            )

    (out_dir / "audit.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    corpus = report["corpus"]
    lines = [
        "# School Days HQ — ORS Event Runtime Audit",
        "",
        f"- ORS files: **{corpus['ors_files']}**",
        f"- Event lines: **{corpus['event_lines']}**",
        f"- Corpus manifest SHA-256: `{corpus['aggregate_manifest_sha256']}`",
        f"- Encodings: `{json.dumps(corpus['encodings'], ensure_ascii=False, sort_keys=True)}`",
        "",
        "## Command coverage",
        "",
        "| Command | Parser | Count | Files | Fields after start | Runtime | Current interpretation |",
        "|---|---:|---:|---:|---|---|---|",
    ]

    def esc(value):
        return str(value).replace("|", "\\|").replace("\n", " ")

    for row in report["commands"]:
        fields = ", ".join(
            f"{key}:{value}" for key, value in row["field_counts_after_start"].items()
        )
        lines.append(
            f"| {esc(row['command'])} | {esc(row['parser_enum'])} | {row['count']} | "
            f"{row['files']} | {esc(fields)} | **{esc(row['runtime_status'])}** | "
            f"{esc(row['notes'])} |"
        )

    lines += ["", "## Unknown headers", ""]
    if report["unknown_headers"]:
        lines += [
            f"- `{item['command']}` — {item['count']} occurrence(s)"
            for item in report["unknown_headers"]
        ]
    else:
        lines.append("- None.")

    lines += [
        "",
        "## Malformed / parser-risk event lines",
        "",
        f"- {len(report['malformed_event_lines'])} detected. See `audit.json` for samples.",
        "",
        "## Status meanings",
        "",
        "- `validated`: in-game behavior gate exists.",
        "- `partial`: code exists but payload/lifetime/original semantics are not closed.",
        "- `boundary-only`: currently used only for scene-boundary timing.",
        "- `suspect`: current implementation likely does not model the intended semantic operation.",
        "- `unsupported`: header is present but parser does not recognize it.",
        "",
        "This audit is read-only evidence; it changes no gameplay behavior.",
    ]
    (out_dir / "SUMMARY.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ors-root", required=True, type=Path)
    parser.add_argument(
        "--out-dir", type=Path, default=Path("build/schooldays-ors-audit")
    )
    parser.add_argument("--sample-limit", type=int, default=5)
    args = parser.parse_args()

    if not args.ors_root.is_dir():
        print(f"ERROR: invalid ORS root: {args.ors_root}", file=sys.stderr)
        return 2
    if args.sample_limit < 0:
        print("ERROR: --sample-limit must be >= 0", file=sys.stderr)
        return 2

    report, rows = audit(args.ors_root, args.sample_limit)
    if report["corpus"]["ors_files"] == 0:
        print("ERROR: no .ORS files found", file=sys.stderr)
        return 3

    write_outputs(report, rows, args.out_dir)
    unknown = report["unknown_headers"]
    print(
        f"[ORS-AUDIT] files={report['corpus']['ors_files']} "
        f"events={report['corpus']['event_lines']}"
    )
    print(
        f"[ORS-AUDIT] unknown_occurrences={sum(x['count'] for x in unknown)} "
        f"distinct_unknown_headers={len(unknown)}"
    )
    print(f"[ORS-AUDIT] output={args.out_dir.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
