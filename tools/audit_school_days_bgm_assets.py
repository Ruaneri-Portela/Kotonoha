"""Read-only audit of ORS BGM resource names against extracted assets."""

import argparse
import collections
import json
from pathlib import Path, PurePosixPath


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("assets", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    root = args.assets
    entries = {}
    collisions = []

    def actual_path(logical):
        current = root
        mismatch = False
        for part in PurePosixPath(logical).parts:
            if current not in entries:
                names = [item.name for item in current.iterdir()] if current.is_dir() else []
                groups = collections.defaultdict(list)
                for name in names:
                    groups[name.casefold()].append(name)
                for group in groups.values():
                    if len(group) > 1:
                        collisions.append({"directory": str(current), "names": group})
                entries[current] = {name.casefold(): name for name in names}
            found = entries[current].get(part.casefold())
            if found is None:
                return None, mismatch
            mismatch |= found != part
            current /= found
        return (str(current), mismatch) if current.is_file() else (None, mismatch)

    rows = []
    ors_files = sorted(root.glob("[0-9][0-9]/*.ORS"))
    for ors in ors_files:
        for number, line in enumerate(
            ors.read_text(encoding="utf-8-sig", errors="replace").splitlines(), 1
        ):
            if not (line.startswith("[PlayBgm]=") or line.startswith("[EndBGM]=")):
                continue
            command = line[1:line.index("]")]
            fields = line.split("=", 1)[1].rstrip(";").split("\t")
            if len(fields) != 3:
                continue
            logical = fields[1]
            suffixes = ("_int.ogg", "_loop.ogg") if command == "PlayBgm" else (".ogg",)
            for suffix in suffixes:
                expected = logical + suffix
                actual, mismatch = actual_path(expected)
                rows.append({
                    "scene": ors.name,
                    "line": number,
                    "command": command,
                    "logical": logical,
                    "expected": expected,
                    "actual": actual,
                    "status": "missing" if actual is None else
                              "case_mismatch" if mismatch else "exact",
                })
    counts = collections.Counter(
        (row["command"], row["expected"][len(row["logical"]):], row["status"])
        for row in rows
    )
    missing = collections.Counter(
        row["logical"] for row in rows if row["status"] == "missing"
    )
    grouped = {}
    for row in rows:
        key = (row["command"], row["logical"], row["expected"], row["actual"], row["status"])
        if key not in grouped:
            grouped[key] = {
                "command": row["command"], "logical": row["logical"],
                "expected": row["expected"], "actual": row["actual"],
                "status": row["status"], "occurrences": 0, "example_scenes": [],
            }
        entry = grouped[key]
        entry["occurrences"] += 1
        if len(entry["example_scenes"]) < 3 and row["scene"] not in entry["example_scenes"]:
            entry["example_scenes"].append(row["scene"])
    report = {
        "files_scanned": len(ors_files),
        "resource_references": len(rows),
        "summary": [
            {"command": command, "suffix": suffix, "status": status, "count": count}
            for (command, suffix, status), count in sorted(counts.items())
        ],
        "missing_paths": dict(missing),
        "case_insensitive_collisions": collisions,
        "unique_resources": list(grouped.values()),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"{len(ors_files)} ORS; {len(rows)} resource references; {len(missing)} missing paths")


if __name__ == "__main__":
    main()
