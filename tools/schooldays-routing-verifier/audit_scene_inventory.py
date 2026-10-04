#!/usr/bin/env python3
"""
Audit the recovered School Days routing SceneKeys against the physical ORS scripts.

This is intentionally independent from the engine's sceneIndex so it can detect
routing nodes that exist in RouteProc data but have no physical Gameplay script,
as well as physical ORS scripts that do not exist in the recovered normal-New-Game
router.

Exit codes:
  0 = inventory matches the current formal baseline
  1 = inventory mismatch / unexpected router-only nodes / parse problem
  2 = input path problem
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

EXPECTED_ROUTER_COUNT = 1857
EXPECTED_ORS_COUNT = 1857
EXPECTED_ROUTER_ONLY = {
    "03/03-B2-A00",
    "03/03-KB-E00",
}
EXPECTED_ORS_ONLY_COUNT = 2

NODE_RE = re.compile(
    r'^\s*\{"([^"]+)",\s*(\d+)u,\s*(\d+)u,\s*(\d+)u\},\s*$'
)
ROUTE_OFFSETS_RE = re.compile(
    r"static constexpr uint32_t kRouteOffsets\[\]\s*=\s*\{(?P<body>.*?)\};",
    re.DOTALL,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Audit School Days router SceneKeys against physical ORS scripts."
    )
    parser.add_argument(
        "--repo",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="Kotonoha repository root (default: inferred from script path).",
    )
    parser.add_argument(
        "--assets",
        type=Path,
        default=None,
        help="Directory containing School Days script roots 00..05. "
             "Default: <repo>/assets",
    )
    parser.add_argument(
        "--generated",
        type=Path,
        default=None,
        help="SchoolDaysRouteData.generated.inc path. "
             "Default: <repo>/src/SchoolDaysRouteData.generated.inc",
    )
    parser.add_argument(
        "--json-out",
        type=Path,
        default=None,
        help="Optional JSON report path.",
    )
    return parser.parse_args()


def parse_generated(path: Path):
    text = path.read_text(encoding="utf-8")

    offsets_match = ROUTE_OFFSETS_RE.search(text)
    if not offsets_match:
        raise ValueError("kRouteOffsets[] not found")

    offsets = [int(x) for x in re.findall(r"\d+", offsets_match.group("body"))]
    if len(offsets) < 2:
        raise ValueError("kRouteOffsets[] is incomplete")

    nodes = []
    in_nodes = False
    for line in text.splitlines():
        if line.startswith("static constexpr NodeData kNodes[]"):
            in_nodes = True
            continue
        if in_nodes and line.strip() == "};":
            break
        if not in_nodes:
            continue

        match = NODE_RE.match(line)
        if not match:
            continue

        nodes.append(
            {
                "scene_key": match.group(1),
                "transition_start": int(match.group(2)),
                "transition_count": int(match.group(3)),
                "choice_mask": int(match.group(4)),
            }
        )

    if not nodes:
        raise ValueError("kNodes[] could not be parsed")

    if offsets[-1] != len(nodes):
        raise ValueError(
            f"kRouteOffsets final value {offsets[-1]} != parsed node count {len(nodes)}"
        )

    # Annotate each node with its RouteProc route/scene coordinates.
    route = 0
    for index, node in enumerate(nodes):
        while route + 1 < len(offsets) and index >= offsets[route + 1]:
            route += 1
        node["node_index"] = index
        node["route"] = route
        node["scene"] = index - offsets[route]

    return offsets, nodes


def scene_key_from_ors(path: Path):
    name = path.name
    upper = name.upper()
    suffix = ".ENG.ORS"

    if not upper.endswith(suffix):
        return None, "not .ENG.ORS"

    group = path.parent.name
    if not re.fullmatch(r"\d{2}", group):
        return None, f"parent directory is not a two-digit episode group: {group!r}"

    stem = name[: -len(suffix)]
    if not stem.startswith(group + "-"):
        return None, (
            f"filename prefix {stem!r} does not match parent group {group!r}"
        )

    return f"{group}/{stem}", None


def scan_physical_ors(assets: Path):
    records = []
    rejected = []

    for episode in [f"{i:02d}" for i in range(6)]:
        root = assets / episode
        if not root.is_dir():
            raise FileNotFoundError(f"missing script directory: {root}")

        for path in sorted(p for p in root.rglob("*") if p.is_file()):
            if not path.name.upper().endswith(".ORS"):
                continue

            scene_key, error = scene_key_from_ors(path)
            if scene_key is None:
                rejected.append(
                    {
                        "path": str(path),
                        "reason": error,
                    }
                )
                continue

            records.append(
                {
                    "scene_key": scene_key,
                    "path": str(path),
                    "episode": episode,
                }
            )

    return records, rejected


def duplicate_values(values):
    counts = Counter(values)
    return sorted(key for key, count in counts.items() if count > 1)


def episode_counts(scene_keys):
    result = Counter()
    for key in scene_keys:
        result[key.split("/", 1)[0]] += 1
    return dict(sorted(result.items()))


def main() -> int:
    args = parse_args()
    repo = args.repo.resolve()
    assets = (args.assets or (repo / "assets")).resolve()
    generated = (
        args.generated or (repo / "src" / "SchoolDaysRouteData.generated.inc")
    ).resolve()
    json_out = (
        args.json_out
        or (repo / "build" / "routing-verifier" / "scene-inventory-audit.json")
    ).resolve()

    if not generated.is_file():
        print(f"ERROR: generated router data not found: {generated}", file=sys.stderr)
        return 2
    if not assets.is_dir():
        print(f"ERROR: assets directory not found: {assets}", file=sys.stderr)
        return 2

    try:
        offsets, nodes = parse_generated(generated)
        ors_records, rejected = scan_physical_ors(assets)
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    router_keys = [node["scene_key"] for node in nodes]
    ors_keys = [record["scene_key"] for record in ors_records]

    router_duplicates = duplicate_values(router_keys)
    ors_duplicates = duplicate_values(ors_keys)

    router_set = set(router_keys)
    ors_set = set(ors_keys)

    router_only = sorted(router_set - ors_set)
    ors_only = sorted(ors_set - router_set)

    router_by_key = {node["scene_key"]: node for node in nodes}
    router_only_details = []
    for key in router_only:
        node = router_by_key[key]
        router_only_details.append(
            {
                "scene_key": key,
                "route": node["route"],
                "scene": node["scene"],
                "node_index": node["node_index"],
                "transition_start": node["transition_start"],
                "transition_count": node["transition_count"],
                "choice_mask": node["choice_mask"],
                "has_outgoing_transition": node["transition_count"] > 0,
            }
        )

    checks = {
        "router_count": len(router_keys) == EXPECTED_ROUTER_COUNT,
        "ors_count": len(ors_keys) == EXPECTED_ORS_COUNT,
        "router_unique": not router_duplicates,
        "ors_unique": not ors_duplicates,
        "ors_parse_clean": not rejected,
        "router_only_exactly_known_set": set(router_only) == EXPECTED_ROUTER_ONLY,
        "ors_only_count": len(ors_only) == EXPECTED_ORS_ONLY_COUNT,
        "router_only_all_have_outgoing": all(
            item["has_outgoing_transition"] for item in router_only_details
        ),
    }

    passed = all(checks.values())

    report = {
        "format": "schooldays-scene-inventory-audit-v1",
        "repo": str(repo),
        "generated": str(generated),
        "assets": str(assets),
        "expected": {
            "router_count": EXPECTED_ROUTER_COUNT,
            "ors_count": EXPECTED_ORS_COUNT,
            "router_only": sorted(EXPECTED_ROUTER_ONLY),
            "ors_only_count": EXPECTED_ORS_ONLY_COUNT,
        },
        "actual": {
            "router_count": len(router_keys),
            "ors_count": len(ors_keys),
            "router_unique_count": len(router_set),
            "ors_unique_count": len(ors_set),
            "router_episode_counts": episode_counts(router_keys),
            "ors_episode_counts": episode_counts(ors_keys),
            "router_only": router_only,
            "router_only_details": router_only_details,
            "ors_only": ors_only,
            "router_duplicates": router_duplicates,
            "ors_duplicates": ors_duplicates,
            "rejected_ors": rejected,
        },
        "checks": checks,
        "result": "PASS" if passed else "FAIL",
    }

    json_out.parent.mkdir(parents=True, exist_ok=True)
    json_out.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    print("School Days Scene Inventory Audit")
    print("=" * 41)
    print(f"Router SceneKeys:       {len(router_keys)}")
    print(f"Physical ORS SceneKeys: {len(ors_keys)}")
    print()
    print("Router-only:")
    if router_only_details:
        for item in router_only_details:
            print(
                "  "
                f"{item['scene_key']} "
                f"(route={item['route']} scene={item['scene']} "
                f"outgoing={item['transition_count']})"
            )
    else:
        print("  <none>")

    print()
    print("ORS-only:")
    if ors_only:
        for key in ors_only:
            print(f"  {key}")
    else:
        print("  <none>")

    if router_duplicates:
        print()
        print("Duplicate router SceneKeys:")
        for key in router_duplicates:
            print(f"  {key}")

    if ors_duplicates:
        print()
        print("Duplicate physical ORS SceneKeys:")
        for key in ors_duplicates:
            print(f"  {key}")

    if rejected:
        print()
        print("Rejected ORS paths:")
        for item in rejected:
            print(f"  {item['path']}: {item['reason']}")

    print()
    print("Checks:")
    for name, ok in checks.items():
        print(f"  {name}: {'PASS' if ok else 'FAIL'}")

    print()
    print(f"JSON report: {json_out}")
    print(f"RESULT: {'PASS' if passed else 'FAIL'}")

    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
