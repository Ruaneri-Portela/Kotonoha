from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path


EXPECTED = {
    "route_count": 55,
    "node_count": 1857,
    "transition_count": 2458,
    "condition_count": 1464,
    "effect_count": 6183,
    "feeling_resolution_count": 772,
    "terminal_transition_count": 23,
    "callback38_terminal_count": 23,
    "callback38_nonterminal_count": 47,
}

ROUTING_ONLY = {
    "03/03-B2-A00",
    "03/03-KB-E00",
}

CANONICAL_FILES = [
    "CMakeLists.txt",
    "include/Kotonoha/SchoolDaysRouter.hpp",
    "include/Kotonoha/Kotonoha.hpp",
    "src/SchoolDaysRouter.cpp",
    "src/SchoolDaysRouteData.generated.inc",
    "src/Kotonoha.cpp",
    "tests/SchoolDaysFullRouterTest.cpp",
    "tests/SchoolDaysHandoffRouterTest.cpp",
    "tests/SchoolDaysDevCheckpointTest.cpp",
    "tools/schooldays-routing-verifier/audit_terminal_callbacks.py",
    "tools/schooldays-routing-verifier/freeze_route_model.py",
    "tools/schooldays-routing-verifier/run_freeze_audit.ps1",
]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def array_body(text: str, typename: str, name: str) -> str:
    pattern = (
        rf"static constexpr {re.escape(typename)} "
        rf"{re.escape(name)}\[\]\s*=\s*\{{(.*?)\n\}};"
    )
    m = re.search(pattern, text, re.S)
    if not m:
        raise RuntimeError(f"array not found: {name}")
    return m.group(1)


def unquote(value: str):
    if value == "nullptr":
        return None
    return value[1:-1]


def run_git(root: Path, *args: str) -> str:
    try:
        return subprocess.check_output(
            ["git", *args],
            cwd=root,
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except Exception:
        return ""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument(
        "--out",
        default="docs/school-days-routing-validation/certified",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    out = (root / args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)

    generated_path = root / "src/SchoolDaysRouteData.generated.inc"
    text = generated_path.read_text(encoding="utf-8")

    offsets_body = array_body(text, "uint32_t", "kRouteOffsets")
    offsets = [int(x) for x in re.findall(r"\d+", offsets_body)]

    nodes = []
    for m in re.finditer(
        r'\{"([^"]+)",\s*(\d+)u,\s*(\d+)u,\s*(\d+)u\}',
        array_body(text, "NodeData", "kNodes"),
    ):
        nodes.append({
            "scene_key": m.group(1),
            "transition_start": int(m.group(2)),
            "transition_count": int(m.group(3)),
            "choice_mask": int(m.group(4)),
        })

    conditions = []
    for m in re.finditer(
        r'\{ConditionKind::(\w+),\s*CompareOp::(\w+),\s*'
        r'(nullptr|"[^"]*"),\s*(nullptr|"[^"]*"),\s*(-?\d+)\}',
        array_body(text, "ConditionData", "kConditions"),
    ):
        conditions.append({
            "kind": m.group(1),
            "op": m.group(2),
            "source_a": unquote(m.group(3)),
            "source_b": unquote(m.group(4)),
            "value": int(m.group(5)),
        })

    effects = []
    for m in re.finditer(
        r'\{EffectKind::(\w+),\s*(nullptr|"[^"]*"),\s*'
        r'(nullptr|"[^"]*"),\s*(-?\d+)\}',
        array_body(text, "EffectData", "kEffects"),
    ):
        effects.append({
            "kind": m.group(1),
            "target": unquote(m.group(2)),
            "source": unquote(m.group(3)),
            "value": int(m.group(4)),
        })

    transitions = []
    for m in re.finditer(
        r'\{(\d+)u,\s*(-?\d+),\s*(-?\d+),\s*'
        r'(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(true|false)\}',
        array_body(text, "TransitionData", "kTransitions"),
    ):
        transitions.append({
            "id": int(m.group(1)),
            "destination_route": int(m.group(2)),
            "destination_scene": int(m.group(3)),
            "condition_start": int(m.group(4)),
            "condition_count": int(m.group(5)),
            "effect_start": int(m.group(6)),
            "effect_count": int(m.group(7)),
            "terminal": m.group(8) == "true",
        })

    feeling_deltas = []
    for m in re.finditer(
        r'\{"([^"]+)",\s*(-?\d+)\}',
        array_body(text, "FeelingDelta", "kFeelingDeltas"),
    ):
        feeling_deltas.append({
            "variable": m.group(1),
            "delta": int(m.group(2)),
        })

    feeling_resolutions = []
    for m in re.finditer(
        r'\{(\d+)u,\s*(-?\d+),\s*(\d+)u,\s*(\d+)u\}',
        array_body(text, "FeelingResolution", "kFeelingResolutions"),
    ):
        feeling_resolutions.append({
            "node_index": int(m.group(1)),
            "choice": int(m.group(2)),
            "delta_start": int(m.group(3)),
            "delta_count": int(m.group(4)),
        })

    checks = []

    def check(name: str, condition: bool, detail: str = ""):
        checks.append({
            "name": name,
            "pass": bool(condition),
            "detail": detail,
        })

    check(
        "route_count",
        len(offsets) - 1 == EXPECTED["route_count"],
        f"{len(offsets) - 1} == {EXPECTED['route_count']}",
    )

    check(
        "node_count",
        len(nodes) == EXPECTED["node_count"],
        f"{len(nodes)} == {EXPECTED['node_count']}",
    )

    check(
        "transition_count",
        len(transitions) == EXPECTED["transition_count"],
        f"{len(transitions)} == {EXPECTED['transition_count']}",
    )

    check(
        "condition_count",
        len(conditions) == EXPECTED["condition_count"],
        f"{len(conditions)} == {EXPECTED['condition_count']}",
    )

    check(
        "effect_count",
        len(effects) == EXPECTED["effect_count"],
        f"{len(effects)} == {EXPECTED['effect_count']}",
    )

    check(
        "feeling_resolution_count",
        len(feeling_resolutions) == EXPECTED["feeling_resolution_count"],
        f"{len(feeling_resolutions)} == {EXPECTED['feeling_resolution_count']}",
    )

    check("offset_first_zero", offsets[0] == 0)
    check("offset_last_matches_nodes", offsets[-1] == len(nodes))
    check(
        "offsets_monotonic",
        all(a <= b for a, b in zip(offsets, offsets[1:])),
    )

    scene_keys = [n["scene_key"] for n in nodes]

    check(
        "scene_keys_unique",
        len(scene_keys) == len(set(scene_keys)),
    )

    check(
        "routing_only_catalog",
        ROUTING_ONLY.issubset(set(scene_keys)),
        ", ".join(sorted(ROUTING_ONLY)),
    )

    check(
        "node_transition_slices",
        all(
            n["transition_start"] + n["transition_count"] <= len(transitions)
            for n in nodes
        ),
    )

    transition_ids = [t["id"] for t in transitions]

    check(
        "transition_ids_unique",
        len(transition_ids) == len(set(transition_ids)),
    )

    check(
        "transition_condition_slices",
        all(
            t["condition_start"] + t["condition_count"] <= len(conditions)
            for t in transitions
        ),
    )

    check(
        "transition_effect_slices",
        all(
            t["effect_start"] + t["effect_count"] <= len(effects)
            for t in transitions
        ),
    )

    destination_ok = True
    for t in transitions:
        if t["terminal"]:
            continue

        r = t["destination_route"]
        s = t["destination_scene"]

        if not 0 <= r < EXPECTED["route_count"]:
            destination_ok = False
            break

        route_size = offsets[r + 1] - offsets[r]
        if not 0 <= s < route_size:
            destination_ok = False
            break

    check("nonterminal_destinations_valid", destination_ok)

    condition_kinds = sorted({c["kind"] for c in conditions})
    compare_ops = sorted({c["op"] for c in conditions})
    effect_kinds = sorted({e["kind"] for e in effects})

    check(
        "condition_kind_catalog",
        set(condition_kinds) == {
            "Choice",
            "SessionValue",
            "SessionComparison",
            "GlobalValue",
            "Callback34",
            "FlagOr",
        },
        str(condition_kinds),
    )

    check(
        "compare_op_catalog",
        set(compare_ops) == {"Eq", "Ne", "Gt", "Le"},
        str(compare_ops),
    )

    check(
        "effect_kind_catalog",
        set(effect_kinds) == {
            "SetSessionConst",
            "SetSessionFromSession",
            "SetGlobalConst",
            "RegisterEnding",
            "Callback",
        },
        str(effect_kinds),
    )

    feeling_ok = all(
        r["node_index"] < len(nodes)
        and r["delta_start"] + r["delta_count"] <= len(feeling_deltas)
        for r in feeling_resolutions
    )
    check("feeling_resolution_slices", feeling_ok)

    transition_source = {}

    for node_index, node in enumerate(nodes):
        start = node["transition_start"]
        end = start + node["transition_count"]

        for transition_index in range(start, end):
            if transition_index in transition_source:
                check(
                    "transition_source_unique",
                    False,
                    f"transition index {transition_index}",
                )
                break
            transition_source[transition_index] = node_index

    check(
        "all_transitions_owned_by_node",
        len(transition_source) == len(transitions),
        f"{len(transition_source)} / {len(transitions)}",
    )

    def transition_effects(t):
        begin = t["effect_start"]
        end = begin + t["effect_count"]
        return effects[begin:end]

    terminal = [t for t in transitions if t["terminal"]]

    callback38_terminal = []
    callback38_nonterminal = []

    for t in transitions:
        callbacks = [
            e["target"]
            for e in transition_effects(t)
            if e["kind"] == "Callback"
        ]

        if "callback_38" in callbacks:
            if t["terminal"]:
                callback38_terminal.append(t)
            else:
                callback38_nonterminal.append(t)

    check(
        "terminal_transition_count",
        len(terminal) == EXPECTED["terminal_transition_count"],
        str(len(terminal)),
    )

    check(
        "terminal_callback38_count",
        len(callback38_terminal) == EXPECTED["callback38_terminal_count"],
        str(len(callback38_terminal)),
    )

    check(
        "nonterminal_callback38_count",
        len(callback38_nonterminal) == EXPECTED["callback38_nonterminal_count"],
        str(len(callback38_nonterminal)),
    )

    router_cpp = (root / "src/SchoolDaysRouter.cpp").read_text(
        encoding="utf-8"
    )
    dev_test = (root / "tests/SchoolDaysDevCheckpointTest.cpp").read_text(
        encoding="utf-8"
    )
    kotonoha_cpp = (root / "src/Kotonoha.cpp").read_text(
        encoding="utf-8"
    )

    for checkpoint in (
        "sd-ep1-r0-l00-handoff",
        "sd-ending16-r49-i02-handoff",
    ):
        check(
            f"router_checkpoint_{checkpoint}",
            checkpoint in router_cpp,
        )
        check(
            f"test_checkpoint_{checkpoint}",
            checkpoint in dev_test,
        )

    check(
        "temporary_ktn_diag_removed",
        "[KTN-DIAG]" not in kotonoha_cpp,
    )

    hashes = {}

    for rel in CANONICAL_FILES:
        path = root / rel

        check(
            f"canonical_file_exists:{rel}",
            path.is_file(),
        )

        if path.is_file():
            hashes[rel] = {
                "sha256": sha256(path),
                "bytes": path.stat().st_size,
            }

    scene_catalog = []

    for route in range(len(offsets) - 1):
        begin = offsets[route]
        end = offsets[route + 1]

        for index in range(begin, end):
            node = nodes[index]

            scene_catalog.append({
                "node_index": index,
                "route": route,
                "scene": index - begin,
                "scene_key": node["scene_key"],
                "choice_mask": node["choice_mask"],
                "transition_ids": [
                    transitions[i]["id"]
                    for i in range(
                        node["transition_start"],
                        node["transition_start"]
                        + node["transition_count"],
                    )
                ],
            })

    ending_map = {}

    for t in terminal:
        registrations = [
            e["value"]
            for e in transition_effects(t)
            if e["kind"] == "RegisterEnding"
        ]

        ending_map[str(t["id"])] = registrations

    passed = all(c["pass"] for c in checks)

    head = run_git(root, "rev-parse", "HEAD")
    branch = run_git(root, "branch", "--show-current")

    freeze = {
        "format": "schooldays-routing-freeze-v1",
        "status": "PASS" if passed else "FAIL",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "scope": "School Days HQ normal New Game routing oracle",
        "git": {
            "branch": branch,
            "source_head_before_oracle_commit": head,
        },
        "counts": {
            "routes": len(offsets) - 1,
            "nodes": len(nodes),
            "transitions": len(transitions),
            "conditions": len(conditions),
            "effects": len(effects),
            "feeling_deltas": len(feeling_deltas),
            "feeling_resolutions": len(feeling_resolutions),
            "terminal_transitions": len(terminal),
            "callback38_terminal": len(callback38_terminal),
            "callback38_nonterminal": len(callback38_nonterminal),
        },
        "catalogs": {
            "condition_kinds": condition_kinds,
            "compare_ops": compare_ops,
            "effect_kinds": effect_kinds,
            "routing_only_scene_keys": sorted(ROUTING_ONLY),
        },
        "checks": checks,
        "hashes_file": "ROUTING_MODEL_HASHES.json",
        "catalog_file": "ROUTING_MODEL_CATALOG.json",
    }

    catalog = {
        "format": "schooldays-routing-catalog-v1",
        "route_offsets": offsets,
        "scenes": scene_catalog,
        "transition_ids": transition_ids,
        "terminal_transition_ids": [t["id"] for t in terminal],
        "callback38_terminal_transition_ids": [
            t["id"] for t in callback38_terminal
        ],
        "callback38_nonterminal_transition_ids": [
            t["id"] for t in callback38_nonterminal
        ],
        "direct_terminal_ending_registrations": ending_map,
    }

    (out / "ROUTING_MODEL_FREEZE.json").write_text(
        json.dumps(freeze, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    (out / "ROUTING_MODEL_HASHES.json").write_text(
        json.dumps(
            {
                "format": "schooldays-routing-hashes-v1",
                "sha256": hashes,
            },
            indent=2,
            ensure_ascii=False,
        ) + "\n",
        encoding="utf-8",
    )

    (out / "ROUTING_MODEL_CATALOG.json").write_text(
        json.dumps(catalog, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    md = [
        "# School Days HQ — Routing Model Freeze",
        "",
        f"Status: **{'PASS' if passed else 'FAIL'}**",
        "",
        "Scope: Normal New Game routing oracle.",
        "",
        "## Frozen counts",
        "",
        f"- Routes: **{len(offsets) - 1}**",
        f"- Nodes / SceneKeys: **{len(nodes)}**",
        f"- Reachable router transitions: **{len(transitions)}**",
        f"- Conditions: **{len(conditions)}**",
        f"- Effects: **{len(effects)}**",
        f"- Feeling resolutions: **{len(feeling_resolutions)}**",
        f"- Terminal transitions: **{len(terminal)}**",
        f"- Non-terminal `callback_38` handoffs: **{len(callback38_nonterminal)}**",
        f"- Terminal `callback_38` handoffs: **{len(callback38_terminal)}**",
        "",
        "## Structural checks",
        "",
    ]

    for c in checks:
        mark = "PASS" if c["pass"] else "FAIL"
        suffix = f" — {c['detail']}" if c["detail"] else ""
        md.append(f"- `{mark}` {c['name']}{suffix}")

    md += [
        "",
        "## Oracle rule",
        "",
        "Any future routing serialization or KTRF implementation must reproduce",
        "this frozen model without changing the certified observable routing",
        "semantics. A deliberate routing-model change requires a new freeze",
        "version rather than silently replacing this oracle.",
        "",
    ]

    (out / "ROUTING_MODEL_FREEZE.md").write_text(
        "\n".join(md),
        encoding="utf-8",
    )

    print("=== SCHOOL DAYS ROUTING FREEZE AUDIT ===")
    print(f"status={'PASS' if passed else 'FAIL'}")
    print(f"routes={len(offsets) - 1}")
    print(f"nodes={len(nodes)}")
    print(f"transitions={len(transitions)}")
    print(f"conditions={len(conditions)}")
    print(f"effects={len(effects)}")
    print(f"feeling_resolutions={len(feeling_resolutions)}")
    print(f"terminal={len(terminal)}")
    print(f"callback38_nonterminal={len(callback38_nonterminal)}")
    print(f"callback38_terminal={len(callback38_terminal)}")
    print(f"artifacts={out}")

    failed = [c for c in checks if not c["pass"]]

    if failed:
        print("")
        print("FAILED CHECKS:")
        for c in failed:
            print(f"- {c['name']}: {c['detail']}")
        return 1

    print("")
    print("FREEZE AUDIT PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
