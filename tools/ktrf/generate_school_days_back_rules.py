#!/usr/bin/env python3
"""Generate immutable School Days Back rule data from authenticated R1 CSVs.

This script intentionally consumes normalized evidence, not the historical
andreili source. It fails closed on schema/count drift so generated runtime
data cannot silently diverge from the reviewed R1 evidence set.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import pathlib
import re
from collections import defaultdict


EXPECTED = {
    "known_scenes": 1857,
    "inverse_effects": 283,
    "sp_mappings": 162,
    "event_rows": 111,  # excludes 55 boundary rows; includes trial marker
    "flag_clear_rows": 78,
    "callback_rows": 30,
    "int_zero_rows": 2,
    "predecessor_actions": 1880,
    "predecessor_conditional_actions": 37,
}


def load_csv(path: pathlib.Path):
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f))


def parse_int(text: str) -> int:
    return int(text, 0)


def cases_for(row):
    values = [x.strip() for x in row["cases"].split(",") if x.strip()]
    if values:
        return [parse_int(x) for x in values]
    m = re.search(r"SCENE\s*==\s*(0x[0-9A-Fa-f]+|\d+)", row["scene_predicate"])
    return [parse_int(m.group(1))] if m else []


def cpp_predicate(pred: str):
    if pred == "TRUE":
        return None, "true"
    symbols = sorted(set(re.findall(r"BS[A-Za-z0-9]+", pred)))
    if len(symbols) != 1:
        raise RuntimeError(f"predicate must reference exactly one BS symbol: {pred}")
    symbol = symbols[0]
    expr = re.sub(rf"\b{re.escape(symbol)}\b", "value", pred)
    expr = re.sub(r"\bAND\b", "&&", expr)
    expr = re.sub(r"\bOR\b", "||", expr)
    expr = re.sub(r"\bNOT\b", "!", expr)
    # Fail closed if normalization ever starts emitting a richer grammar.
    residue = re.sub(r"value|0x[0-9A-Fa-f]+|\d+|==|!=|&&|\|\||!|\(|\)|\s+", "", expr)
    if residue:
        raise RuntimeError(f"unsupported predicate grammar ({residue!r}): {pred}")
    return symbol, expr


def sha(path: pathlib.Path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--evidence-dir", required=True, type=pathlib.Path)
    ap.add_argument("--output", required=True, type=pathlib.Path)
    args = ap.parse_args()
    base = args.evidence_dir

    inverse_path = base / "back-inverse-feelings-r1.csv"
    events_path = base / "back-conditional-events-r1.csv"
    sp_path = base / "sp-scene-map.csv"
    helpers_path = base / "predecessor-helper-routes.csv"
    predecessor_actions_path = base / "back-predecessor-actions-r1.csv"
    inverse = load_csv(inverse_path)
    events = load_csv(events_path)
    sp = load_csv(sp_path)
    helpers = {r["helper_address"].lower(): int(r["destination_route"])
               for r in load_csv(helpers_path)}
    predecessor_actions = load_csv(predecessor_actions_path)

    known = []
    feelings = []
    allowed = set()
    for row in inverse:
        route, scene = int(row["route"]), int(row["scene"])
        if row["scene_key_valid"] == "True":
            known.append((route, scene, row["scene_key"]))
        for effect in json.loads(row["inverse_effects"] or "[]"):
            var = int(effect["variable"])
            if var < 0 or var >= 5:
                raise RuntimeError(f"unexpected feeling variable {var:03d}")
            delta = int(effect["state_delta"])
            if delta:
                feelings.append((route, scene, var, delta))
        for call in filter(None, row["predecessor_calls"].split("|")):
            helper = call.split(":", 1)[0].lower()
            if helper not in helpers:
                raise RuntimeError(f"unknown predecessor helper {helper}")
            allowed.add((route, scene, helpers[helper]))

    sp_rows = sorted((int(r["route"]), int(r["scene"]), int(r["sp_id"])) for r in sp)

    predicate_ids = {"TRUE": 0}
    predicates = [(None, "true", "TRUE")]
    predecessor_rows = []
    for row in predecessor_actions:
        pred = row["state_predicate"]
        if pred not in predicate_ids:
            predicate_ids[pred] = len(predicates)
            symbol, expr = cpp_predicate(pred)
            predicates.append((symbol, expr, pred))
        kind = row["destination_scene_kind"]
        if kind == "literal":
            scene_kind = 0
            literal_scene = int(row["destination_scene_value"], 0)
            scene_symbol = ""
        elif kind == "session_symbol":
            scene_kind = 1
            literal_scene = 0
            scene_symbol = row["destination_scene_value"]
            if not re.fullmatch(r"BS[0-9A-Z]+", scene_symbol):
                raise RuntimeError(f"unexpected predecessor session symbol {scene_symbol!r}")
        else:
            raise RuntimeError(f"unsupported predecessor scene kind {kind!r}")
        predecessor_rows.append((
            int(row["source_route"]),
            int(row["source_scene"]),
            int(row["destination_route"]),
            scene_kind,
            literal_scene,
            scene_symbol,
            predicate_ids[pred],
        ))
    event_rows = []
    kind_code = {
        "session_flag_clear": 1,
        "direct_int_zero": 2,
        "callback_2c": 3,
        "trial_pv_helper_100066c0": 4,
    }
    for row in events:
        kind = row["event_kind"]
        if kind == "boundary_helper_10006760":
            continue
        if kind not in kind_code:
            raise RuntimeError(f"unsupported event kind {kind}")
        pred = row["state_predicate"]
        if pred not in predicate_ids:
            predicate_ids[pred] = len(predicates)
            symbol, expr = cpp_predicate(pred)
            predicates.append((symbol, expr, pred))
        scenes = cases_for(row)
        if not scenes:
            raise RuntimeError(f"event has no concrete scene: {row}")
        target = int(row["target"]) if row["target"] else -1
        for scene in scenes:
            event_rows.append((int(row["route"]), scene, kind_code[kind], target,
                               predicate_ids[pred]))

    checks = {
        "known_scenes": len(known),
        "inverse_effects": len(feelings),
        "sp_mappings": len(sp_rows),
        "event_rows": sum(1 for r in events if r["event_kind"] != "boundary_helper_10006760"),
        "flag_clear_rows": sum(r["event_kind"] == "session_flag_clear" for r in events),
        "callback_rows": sum(r["event_kind"] == "callback_2c" for r in events),
        "int_zero_rows": sum(r["event_kind"] == "direct_int_zero" for r in events),
        "predecessor_actions": len(predecessor_rows),
        "predecessor_conditional_actions": sum(r["state_predicate"] != "TRUE"
                                                 for r in predecessor_actions),
    }
    if checks != EXPECTED:
        raise RuntimeError(f"R1 evidence count drift: got {checks}, expected {EXPECTED}")
    if len(set((route, scene) for route, scene, _ in known)) != len(known):
        raise RuntimeError("duplicate known route/scene")
    if len(set(sp_rows)) != len(sp_rows):
        raise RuntimeError("duplicate SP row")

    out = []
    out.append("// GENERATED FILE. DO NOT EDIT BY HAND.\n")
    out.append("// Source: authenticated RouteProcSDHQ.dll R1 REA/Ghidra normalization.\n")
    for p in (inverse_path, events_path, sp_path, helpers_path,
              predecessor_actions_path):
        out.append(f"// {p.name} SHA256 {sha(p)}\n")
    out.append("\n")
    out.append("static constexpr KnownScene kKnownScenes[] = {\n")
    for route, scene, scene_key in sorted(known):
        escaped = scene_key.replace("\\", "\\\\").replace('"', '\\"')
        out.append(f'    {{{route}, {scene}, "{escaped}"}},\n')
    out.append("};\n\n")
    out.append("static constexpr FeelingEffect kFeelingEffects[] = {\n")
    out.extend(f"    {{{r}, {s}, {v}, {d}}},\n" for r, s, v, d in sorted(feelings))
    out.append("};\n\n")
    out.append("static constexpr SpMapping kSpMappings[] = {\n")
    out.extend(f"    {{{r}, {s}, {spid}}},\n" for r, s, spid in sp_rows)
    out.append("};\n\n")
    out.append("static constexpr AllowedRoute kAllowedRoutes[] = {\n")
    out.extend(f"    {{{r}, {s}, {d}}},\n" for r, s, d in sorted(allowed))
    out.append("};\n\n")
    out.append("static constexpr BackEvent kBackEvents[] = {\n")
    out.extend(f"    {{{r}, {s}, {k}, {t}, {p}}},\n"
               for r, s, k, t, p in event_rows)
    out.append("};\n\n")
    out.append("static constexpr PredecessorAction kPredecessorActions[] = {\n")
    for route, scene, dest_route, scene_kind, literal_scene, scene_symbol, pred in predecessor_rows:
        escaped = scene_symbol.replace("\\", "\\\\").replace('"', '\\"')
        out.append(
            f'    {{{route}, {scene}, {dest_route}, {scene_kind}, '
            f'{literal_scene}, "{escaped}", {pred}}},\n'
        )
    out.append("};\n\n")
    out.append("static bool EvaluateGeneratedPredicate(uint16_t id, const SchoolDaysStateStore* state, bool* result, Kotonoha_KtrfError* error) {\n")
    out.append("    if (state == nullptr || result == nullptr) return false;\n")
    out.append("    switch (id) {\n")
    out.append("    case 0: *result = true; return true;\n")
    for pid, (symbol, expr, source) in enumerate(predicates[1:], 1):
        out.append(f"    case {pid}: {{ // {source}\n")
        out.append("        int64_t value = 0;\n")
        out.append(f"        if (!state->ReadSessionInt(\"{symbol}\", &value, error)) return false;\n")
        out.append(f"        *result = {expr};\n")
        out.append("        return true;\n    }\n")
    out.append("    default: return false;\n    }\n}\n")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("".join(out), encoding="utf-8", newline="\n")
    print(json.dumps({
        **checks,
        "expanded_events": len(event_rows),
        "predicates": len(predicates),
        "allowed_routes": len(allowed),
        "predecessor_actions": len(predecessor_rows),
        "output_sha256": sha(args.output),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
