"""Read-only inventory of ORS media paths against extracted desktop assets.

This is a corpus audit, not a substitute for the C ORS parser. It considers only
tab-delimited rows of the exact resource-bearing arity already accepted by it.
"""

import argparse
import collections
import json
from pathlib import Path


FIELDS = {
    "CreateBG": (4, 2),
    "PlaySe": (4, 2),
    "PlayMovie": (4, 1),
    "PlayBgm": (3, 1),
    "PlayVoice": (5, 1),
    "EndBGM": (3, 1),
    "EndRoll": (3, 1),
}


def resource_names(command, stem):
    if command == "PlayBgm":
        return [stem + "_int.ogg", stem + "_loop.ogg"]
    if command == "EndBGM":
        return [stem + ".ogg"]
    if command in ("PlayMovie", "EndRoll"):
        return [stem + ".wmv"]
    if command == "CreateBG":
        return [stem + ".PNG"]
    # Voice/SE use BuildString, which strips UNC_ from the basename.
    head, _, tail = stem.rpartition("/")
    if tail.startswith("UNC_"):
        stem = (head + "/" if head else "") + tail[4:]
    return [stem + ".OGG"]


class Resolver:
    def __init__(self, root):
        self.root = root
        self.directories = {}

    def resolve(self, logical):
        current = self.root
        mismatch = False
        for part in logical.split("/"):
            if current not in self.directories:
                try:
                    self.directories[current] = collections.defaultdict(list)
                    for entry in current.iterdir():
                        self.directories[current][entry.name.casefold()].append(entry.name)
                except OSError:
                    self.directories[current] = {}
            matches = self.directories[current].get(part.casefold(), [])
            if not matches:
                return "missing", str(current / part)
            if len(matches) > 1:
                return "collision", str(current / part)
            if matches[0] != part:
                mismatch = True
            current = current / matches[0]
        return ("case_mismatch" if mismatch else "exact", str(current)) if current.is_file() else ("missing", str(current))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("assets", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    resolver = Resolver(args.assets.resolve())
    by_type = collections.defaultdict(lambda: collections.Counter())
    samples = collections.defaultdict(list)
    unique = set()
    unique_by_status = collections.defaultdict(set)
    for ors in args.assets.rglob("*.ORS"):
        for line in ors.read_text(encoding="utf-8-sig", errors="replace").splitlines():
            if not line.startswith("[") or "]=" not in line:
                continue
            command, payload = line[1:].split("]=", 1)
            spec = FIELDS.get(command)
            if spec is None:
                continue
            fields = payload.removesuffix(";").split("\t")
            if len(fields) != spec[0]:
                continue
            for logical in resource_names(command, fields[spec[1]]):
                status, physical = resolver.resolve(logical)
                by_type[command][status] += 1
                unique.add((command, logical))
                unique_by_status[(command, status)].add(logical)
                if status != "exact" and len(samples[(command, status)]) < 12:
                    samples[(command, status)].append({
                        "ors": ors.relative_to(args.assets).as_posix(),
                        "logical": logical,
                        "physical_or_candidate": Path(physical).relative_to(resolver.root).as_posix(),
                    })
    result = {
        "assets_root": args.assets.as_posix(),
        "lookup_count": sum(sum(c.values()) for c in by_type.values()),
        "unique_logical_resources": len(unique),
        "by_command": {key: dict(counts) for key, counts in sorted(by_type.items())},
        "unique_by_command_and_status": {
            f"{command}:{status}": len(resources)
            for (command, status), resources in sorted(unique_by_status.items())
        },
        "missing_unique_resources": {
            command: sorted(resources)
            for (command, status), resources in sorted(unique_by_status.items())
            if status == "missing"
        },
        "nonexact_samples": {
            f"{command}:{status}": rows
            for (command, status), rows in sorted(samples.items())
        },
        "note": "Parser-accepted tab rows only; physical availability is not proof of runtime activation.",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"lookups": result["lookup_count"], "by_command": result["by_command"]}))


if __name__ == "__main__":
    main()
