#!/usr/bin/env python3
"""
Classify ORS-only School Days scripts without confusing media paths with script-control references.

The previous classifier matched any occurrence of another ORS basename in an ORS line.
That produced a false positive for 05-9O-B00 because 05-9O-A00 references a VOICE asset
whose directory happens to include "05-9O-B00". This version classifies references by
command type and never treats media-path mentions as script-to-script control flow.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import Counter
from pathlib import Path

from audit_scene_inventory import parse_generated, scan_physical_ors

KNOWN_PARSER_COMMANDS = {
    "CreateBG", "PlaySe", "PlayMovie", "BlackFade", "WhiteFade",
    "PlayBgm", "PrintText", "PlayVoice", "SkipFRAME", "EndBGM",
    "EndRoll", "Next", "SetSELECT",
}

MEDIA_COMMANDS = {
    "CreateBG", "PlaySe", "PlayMovie", "PlayBgm",
    "PlayVoice", "EndBGM", "EndRoll",
}

COMMAND_RE = re.compile(r"^\s*\[([^\]]+)\]")


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    p.add_argument("--assets", type=Path, default=None)
    p.add_argument("--generated", type=Path, default=None)
    p.add_argument("--json-out", type=Path, default=None)
    return p.parse_args()


def read_text(path: Path) -> str:
    data = path.read_bytes()
    for enc in ("utf-8-sig", "utf-8", "cp932", "latin-1"):
        try:
            return data.decode(enc)
        except UnicodeDecodeError:
            pass
    return data.decode("latin-1", errors="replace")


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def structure(path: Path):
    counts = Counter()
    unknown = Counter()
    text = read_text(path)
    for line in text.splitlines():
        m = COMMAND_RE.match(line)
        if not m:
            continue
        cmd = m.group(1)
        counts[cmd] += 1
        if cmd not in KNOWN_PARSER_COMMANDS:
            unknown[cmd] += 1
    return {
        "sha256": sha256(path),
        "size_bytes": path.stat().st_size,
        "line_count": len(text.splitlines()),
        "event_count": sum(counts.values()),
        "command_counts": dict(sorted(counts.items())),
        "unknown_command_counts": dict(sorted(unknown.items())),
    }


def basename(scene_key: str) -> str:
    return scene_key.split("/", 1)[1]


def main() -> int:
    args = parse_args()
    repo = args.repo.resolve()
    assets = (args.assets or repo / "assets").resolve()
    generated = (args.generated or repo / "src" / "SchoolDaysRouteData.generated.inc").resolve()
    out = (args.json_out or repo / "build" / "routing-verifier" /
           "ors-only-classification-v2.json").resolve()

    _, nodes = parse_generated(generated)
    physical, rejected = scan_physical_ors(assets)
    if rejected:
        raise SystemExit("Malformed/rejected ORS paths exist.")

    router = {n["scene_key"] for n in nodes}
    by_key = {r["scene_key"]: r for r in physical}
    ors_only = sorted(set(by_key) - router)

    target_bases = {key: basename(key) for key in ors_only}

    media_mentions = {key: [] for key in ors_only}
    nonmedia_mentions = {key: [] for key in ors_only}

    for src in physical:
        p = Path(src["path"])
        text = read_text(p)
        for line_no, line in enumerate(text.splitlines(), 1):
            m = COMMAND_RE.match(line)
            cmd = m.group(1) if m else "<no-command>"
            lower = line.lower()

            for key, base in target_bases.items():
                if base.lower() not in lower:
                    continue
                if src["scene_key"] == key:
                    continue

                rec = {
                    "source_scene": src["scene_key"],
                    "source_path": str(p),
                    "line": line_no,
                    "command": cmd,
                }

                if cmd in MEDIA_COMMANDS:
                    media_mentions[key].append(rec)
                else:
                    nonmedia_mentions[key].append(rec)

    result = {
        "format": "schooldays-ors-only-classification-v2",
        "router_scene_count": len(nodes),
        "physical_ors_count": len(physical),
        "ors_only_count": len(ors_only),
        "ors_only": [],
    }

    for key in ors_only:
        p = Path(by_key[key]["path"])
        st = structure(p)
        item = {
            "scene_key": key,
            "path": str(p),
            "structural": st,
            "media_path_mentions": media_mentions[key],
            "nonmedia_mentions": nonmedia_mentions[key],
            "control_flow_literal_reference_found": bool(nonmedia_mentions[key]),
        }
        result["ors_only"].append(item)

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print("School Days ORS-only Classification v2")
    print("=" * 40)
    for item in result["ors_only"]:
        print(item["scene_key"])
        print(f"  media-path mentions: {len(item['media_path_mentions'])}")
        for m in item["media_path_mentions"]:
            print(f"    {m['source_scene']}:{m['line']} [{m['command']}]")
        print(f"  non-media/control-like mentions: {len(item['nonmedia_mentions'])}")
        for m in item["nonmedia_mentions"]:
            print(f"    {m['source_scene']}:{m['line']} [{m['command']}]")
        print(f"  literal control-flow reference: {'YES' if item['control_flow_literal_reference_found'] else 'NO'}")
        print()

    print(f"JSON report: {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
