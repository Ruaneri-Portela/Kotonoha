#!/usr/bin/env python3
"""Audit terminal transitions and callback effects in the generated SDHQ router.

This is a structural audit only. It does not claim the runtime/UI semantics of
callback_38; it identifies exactly where the recovered model invokes it.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def array_body(text: str, typename: str, name: str) -> str:
    pattern = rf"static constexpr {re.escape(typename)} {re.escape(name)}\[\]\s*=\s*\{{(.*?)\n\}};"
    match = re.search(pattern, text, re.S)
    if not match:
        raise RuntimeError(f"array not found: {name}")
    return match.group(1)


def unquote(value: str):
    if value == "nullptr":
        return None
    return value[1:-1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "generated",
        nargs="?",
        default="src/SchoolDaysRouteData.generated.inc",
        help="path to SchoolDaysRouteData.generated.inc",
    )
    args = parser.parse_args()

    path = Path(args.generated)
    text = path.read_text(encoding="utf-8")

    offsets_match = re.search(
        r"static constexpr uint32_t kRouteOffsets\[\]\s*=\s*\{(.*?)\};",
        text,
        re.S,
    )
    if not offsets_match:
        raise RuntimeError("kRouteOffsets not found")
    offsets = [int(x) for x in re.findall(r"\d+", offsets_match.group(1))]

    nodes = []
    for match in re.finditer(
        r'\{"([^"]+)",\s*(\d+)u,\s*(\d+)u,\s*(\d+)u\}',
        array_body(text, "NodeData", "kNodes"),
    ):
        nodes.append(
            {
                "scene": match.group(1),
                "transition_start": int(match.group(2)),
                "transition_count": int(match.group(3)),
                "choice_mask": int(match.group(4)),
            }
        )

    transitions = []
    for match in re.finditer(
        r'\{(\d+)u,\s*(-?\d+),\s*(-?\d+),\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(\d+)u,\s*(true|false)\}',
        array_body(text, "TransitionData", "kTransitions"),
    ):
        transitions.append(
            {
                "id": int(match.group(1)),
                "dest_route": int(match.group(2)),
                "dest_scene": int(match.group(3)),
                "condition_start": int(match.group(4)),
                "condition_count": int(match.group(5)),
                "effect_start": int(match.group(6)),
                "effect_count": int(match.group(7)),
                "terminal": match.group(8) == "true",
            }
        )

    effects = []
    for match in re.finditer(
        r'\{EffectKind::(\w+),\s*(nullptr|"[^"]*"),\s*(nullptr|"[^"]*"),\s*(-?\d+)\}',
        array_body(text, "EffectData", "kEffects"),
    ):
        effects.append(
            {
                "kind": match.group(1),
                "target": unquote(match.group(2)),
                "source": unquote(match.group(3)),
                "value": int(match.group(4)),
            }
        )

    transition_source = {}
    for node_index, node in enumerate(nodes):
        start = node["transition_start"]
        end = start + node["transition_count"]
        for transition_index in range(start, end):
            transition_source[transition_index] = node_index

    def route_scene(node_index: int):
        for route in range(len(offsets) - 1):
            if offsets[route] <= node_index < offsets[route + 1]:
                return route, node_index - offsets[route]
        return None, None

    def transition_effects(transition):
        start = transition["effect_start"]
        end = start + transition["effect_count"]
        return effects[start:end]

    terminal_rows = []
    callback38_rows = []

    for transition_index, transition in enumerate(transitions):
        source_index = transition_source.get(transition_index)
        if source_index is None:
            continue
        route, scene = route_scene(source_index)
        source_key = nodes[source_index]["scene"]
        eff = transition_effects(transition)
        callbacks = [e["target"] for e in eff if e["kind"] == "Callback"]
        endings = [e["value"] for e in eff if e["kind"] == "RegisterEnding"]
        row = {
            "transition": transition,
            "route": route,
            "scene": scene,
            "source_key": source_key,
            "effects": eff,
            "callbacks": callbacks,
            "endings": endings,
        }
        if transition["terminal"]:
            terminal_rows.append(row)
        if "callback_38" in callbacks:
            callback38_rows.append(row)

    print(f"Generated file: {path}")
    print(f"Terminal transitions: {len(terminal_rows)}")
    print(f"Transitions invoking callback_38: {len(callback38_rows)}")
    print(
        "Terminal transitions invoking callback_38: "
        f"{sum('callback_38' in r['callbacks'] for r in terminal_rows)} / {len(terminal_rows)}"
    )
    print()

    print("=== TERMINAL TRANSITIONS ===")
    for row in terminal_rows:
        t = row["transition"]
        print(
            f"t{t['id']}  source={row['source_key']}  "
            f"route={row['route']} scene={row['scene']}  "
            f"ending={row['endings'] or '-'}  callbacks={row['callbacks'] or '-'}"
        )
        for effect in row["effects"]:
            print(
                "    "
                f"{effect['kind']} target={effect['target']} "
                f"source={effect['source']} value={effect['value']}"
            )

    print()
    print("=== NON-TERMINAL callback_38 TRANSITIONS ===")
    nonterminal = [r for r in callback38_rows if not r["transition"]["terminal"]]
    print(f"count={len(nonterminal)}")
    for row in nonterminal:
        t = row["transition"]
        print(
            f"t{t['id']}  source={row['source_key']}  "
            f"route={row['route']} scene={row['scene']}  "
            f"dest={t['dest_route']}/{t['dest_scene']}"
        )

    terminal_without_38 = [r for r in terminal_rows if "callback_38" not in r["callbacks"]]
    terminal_without_ending = [r for r in terminal_rows if not r["endings"]]

    print()
    print("=== SUMMARY ===")
    print(f"terminal_without_callback_38={len(terminal_without_38)}")
    print(f"terminal_without_RegisterEnding={len(terminal_without_ending)}")
    print(f"unique_terminal_endings={sorted({e for r in terminal_rows for e in r['endings']})}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
