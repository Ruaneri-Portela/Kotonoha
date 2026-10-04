#!/usr/bin/env python3
"""
Classify physical ORS scripts that are absent from the recovered normal-New-Game router.

This tool deliberately avoids printing dialogue or media payloads. It emits only
structural metadata, hashes, command counts, filenames, and literal-reference
locations so the result can be documented without redistributing game script text.
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

COMMAND_RE = re.compile(r"^\s*\[([^\]]+)\]")
TIMESTAMP_RE = re.compile(r"=\s*([^;\t]+)")


def parse_args():
    p = argparse.ArgumentParser(
        description="Classify ORS-only School Days scripts without exposing payload text."
    )
    p.add_argument(
        "--repo",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    p.add_argument("--assets", type=Path, default=None)
    p.add_argument("--generated", type=Path, default=None)
    p.add_argument("--json-out", type=Path, default=None)
    return p.parse_args()


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_text(path: Path) -> str:
    # The scripts used by Kotonoha are textual ORS. UTF-8 is the expected
    # working encoding; fall back only for structural inspection.
    data = path.read_bytes()
    for enc in ("utf-8-sig", "utf-8", "cp932", "latin-1"):
        try:
            return data.decode(enc)
        except UnicodeDecodeError:
            pass
    return data.decode("latin-1", errors="replace")


def structural_summary(path: Path):
    text = read_text(path)
    command_counts = Counter()
    unknown_counts = Counter()
    first = None
    last = None
    command_rows = []

    for line_no, line in enumerate(text.splitlines(), 1):
        m = COMMAND_RE.match(line)
        if not m:
            continue
        command = m.group(1)
        command_counts[command] += 1
        if command not in KNOWN_PARSER_COMMANDS:
            unknown_counts[command] += 1

        tm = TIMESTAMP_RE.search(line)
        timestamp = tm.group(1).strip() if tm else None
        row = {
            "line": line_no,
            "command": command,
            "timestamp": timestamp,
        }
        command_rows.append(row)
        if first is None:
            first = row
        last = row

    return {
        "sha256": sha256(path),
        "size_bytes": path.stat().st_size,
        "line_count": len(text.splitlines()),
        "event_count": sum(command_counts.values()),
        "command_counts": dict(sorted(command_counts.items())),
        "unknown_command_counts": dict(sorted(unknown_counts.items())),
        "first_event": first,
        "last_event": last,
        "has_next": command_counts["Next"] > 0,
        "next_count": command_counts["Next"],
        "setselect_count": command_counts["SetSELECT"],
        "printtext_count": command_counts["PrintText"],
        "playvoice_count": command_counts["PlayVoice"],
        "playmovie_count": command_counts["PlayMovie"],
        "endroll_count": command_counts["EndRoll"],
    }


def literal_needles(scene_key: str, path: Path):
    # Include forms likely to occur in engine/script metadata.
    base = path.name
    stem = base
    upper = stem.upper()
    if upper.endswith(".ENG.ORS"):
        stem = stem[:-8]
    elif upper.endswith(".ORS"):
        stem = stem[:-4]

    episode, short = scene_key.split("/", 1)
    return sorted({
        scene_key,
        short,
        stem,
        base,
        f"{episode}/{short}",
    })


def scan_references(root_files, targets):
    results = {key: [] for key in targets}
    for path in root_files:
        try:
            text = read_text(path)
        except OSError:
            continue
        lower = text.lower()
        for key, needles in targets.items():
            matched = []
            for needle in needles:
                if needle and needle.lower() in lower:
                    matched.append(needle)
            if not matched:
                continue

            # Record matching line numbers only, never source payload text.
            lines = text.splitlines()
            line_numbers = []
            low_needles = [n.lower() for n in matched]
            for i, line in enumerate(lines, 1):
                l = line.lower()
                if any(n in l for n in low_needles):
                    line_numbers.append(i)
            results[key].append({
                "path": str(path),
                "line_numbers": line_numbers[:100],
                "matched_forms": sorted(set(matched)),
            })
    return results


def source_files(repo: Path, assets: Path):
    allowed = {
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".java",
        ".kt", ".kts", ".gradle", ".cmake", ".md", ".py", ".ps1",
        ".json", ".txt",
    }
    files = []
    for path in repo.rglob("*"):
        if not path.is_file():
            continue
        try:
            path.relative_to(assets)
            continue
        except ValueError:
            pass

        parts = set(path.parts)
        if ".git" in parts or "build" in parts or ".gradle" in parts:
            continue
        if path.suffix.lower() in allowed or path.name == "CMakeLists.txt":
            files.append(path)
    return files


def neighbors(physical_records, target_path: Path):
    same_dir = sorted(Path(r["path"]) for r in physical_records
                      if Path(r["path"]).parent == target_path.parent)
    try:
        idx = same_dir.index(target_path)
    except ValueError:
        return []
    lo = max(0, idx - 3)
    hi = min(len(same_dir), idx + 4)
    return [p.name for p in same_dir[lo:hi] if p != target_path]


def main() -> int:
    args = parse_args()
    repo = args.repo.resolve()
    assets = (args.assets or repo / "assets").resolve()
    generated = (args.generated or repo / "src" / "SchoolDaysRouteData.generated.inc").resolve()
    out = (args.json_out or repo / "build" / "routing-verifier" /
           "ors-only-classification.json").resolve()

    _, nodes = parse_generated(generated)
    physical, rejected = scan_physical_ors(assets)
    if rejected:
        raise SystemExit("Refusing classification: malformed/rejected ORS paths exist.")

    router = {n["scene_key"] for n in nodes}
    by_key = {r["scene_key"]: r for r in physical}
    physical_set = set(by_key)
    ors_only = sorted(physical_set - router)

    target_needles = {}
    summaries = {}
    for key in ors_only:
        p = Path(by_key[key]["path"]).resolve()
        target_needles[key] = literal_needles(key, p)
        summaries[key] = {
            "scene_key": key,
            "path": str(p),
            "structural": structural_summary(p),
            "neighbor_files": neighbors(physical, p),
        }

    ors_paths = [Path(r["path"]).resolve() for r in physical]
    ors_refs = scan_references(ors_paths, target_needles)
    src_refs = scan_references(source_files(repo, assets), target_needles)

    for key in ors_only:
        target_path = Path(summaries[key]["path"])
        # Remove self-reference caused by the filename being represented in a
        # report/source file or by a script payload that names itself.
        summaries[key]["literal_references_in_ors"] = [
            r for r in ors_refs[key] if Path(r["path"]).resolve() != target_path
        ]
        summaries[key]["literal_references_in_repo_source"] = src_refs[key]

        ext_ref = (
            len(summaries[key]["literal_references_in_ors"]) > 0
            or len(summaries[key]["literal_references_in_repo_source"]) > 0
        )
        summaries[key]["reference_scan_class"] = (
            "literal-reference-found" if ext_ref
            else "no-literal-reference-found"
        )

    report = {
        "format": "schooldays-ors-only-classification-v1",
        "scope": "physical ORS absent from recovered normal-New-Game router",
        "privacy": "structural metadata only; no dialogue/media payload emitted",
        "router_scene_count": len(nodes),
        "physical_ors_count": len(physical),
        "ors_only_count": len(ors_only),
        "ors_only": [summaries[k] for k in ors_only],
    }

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print("School Days ORS-only Classification")
    print("=" * 37)
    print(f"ORS-only count: {len(ors_only)}")
    print()

    for key in ors_only:
        item = summaries[key]
        st = item["structural"]
        print(key)
        print(f"  file: {item['path']}")
        print(f"  sha256: {st['sha256']}")
        print(f"  bytes: {st['size_bytes']}")
        print(f"  lines: {st['line_count']}")
        print(f"  events: {st['event_count']}")
        print(f"  Next: {st['next_count']}")
        print(f"  SetSELECT: {st['setselect_count']}")
        print(f"  PrintText: {st['printtext_count']}")
        print(f"  PlayVoice: {st['playvoice_count']}")
        print(f"  PlayMovie: {st['playmovie_count']}")
        print(f"  EndRoll: {st['endroll_count']}")
        print(f"  commands: {json.dumps(st['command_counts'], ensure_ascii=False)}")
        print(f"  unknown commands: {json.dumps(st['unknown_command_counts'], ensure_ascii=False)}")
        print(f"  ORS literal refs: {len(item['literal_references_in_ors'])}")
        for r in item["literal_references_in_ors"]:
            print(f"    - {r['path']} lines={r['line_numbers']} forms={r['matched_forms']}")
        print(f"  repo literal refs: {len(item['literal_references_in_repo_source'])}")
        for r in item["literal_references_in_repo_source"]:
            print(f"    - {r['path']} lines={r['line_numbers']} forms={r['matched_forms']}")
        print(f"  reference scan: {item['reference_scan_class']}")
        print(f"  neighbors: {', '.join(item['neighbor_files'])}")
        print()

    print(f"JSON report: {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
