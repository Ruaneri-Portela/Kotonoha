#!/usr/bin/env python3
"""Profile-level conformance validator for School Days HQ KTRF Routing IR.

This validator is deliberately stricter than the generic KTRF semantic
validator. It checks that the exported IR still carries the complete frozen
School Days executable-oracle inventory and the profile-specific lowering
invariants.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Mapping


ORACLE_COMMIT = "614461c2b14951ba117b9d2dedb4983cfa8ae8e6"
EXPECTED = {
    "routes": 55,
    "nodes": 1857,
    "transitions": 2458,
    "conditions": 1464,
    "effects": 6183,
    "feeling_deltas": 302,
    "feeling_resolutions": 772,
    "choices": 287,
    "endings": 22,
    "callback38_nonterminal": 47,
    "callback38_terminal": 23,
}
ROUTING_ONLY = {"03/03-B2-A00", "03/03-KB-E00"}
EXCLUDED_STRUCTURAL_DEAD = {971, 1243, 1248, 1494, 1824, 1825}
EXPECTED_HOOK_SYMBOLS = {
    "overflow.sdhq:callback_00",
    "overflow.sdhq:callback_2C",
    "overflow.sdhq:callback_38",
}
RESET_EFFECT_ID = "sdhq:effect:profile:reset-choice-result"


class ProfileValidator:
    def __init__(self, document: Mapping[str, Any]):
        self.doc = document
        self.errors: list[str] = []

    def error(self, message: str) -> None:
        self.errors.append(message)

    def records(self, name: str) -> list[Mapping[str, Any]]:
        value = self.doc.get(name, [])
        if not isinstance(value, list):
            return []
        return [x for x in value if isinstance(x, Mapping)]

    def by_id(self, name: str) -> dict[str, Mapping[str, Any]]:
        out: dict[str, Mapping[str, Any]] = {}
        for row in self.records(name):
            row_id = row.get("id")
            if isinstance(row_id, str):
                out[row_id] = row
        return out

    def validate(self) -> list[str]:
        self.validate_identity()
        self.validate_counts()
        self.validate_source_metadata()
        self.validate_nodes_resources()
        self.validate_endings_hooks()
        self.validate_choice_resolution_coverage()
        self.validate_transition_lowering()
        self.validate_callback38()
        self.validate_entry_and_defaults()
        return self.errors

    def validate_identity(self) -> None:
        if self.doc.get("format") != "ktrf-routing-ir":
            self.error("format must be ktrf-routing-ir")
        if self.doc.get("ir_version") != "0.1.0":
            self.error("School Days profile currently requires IR 0.1.0")
        profile = self.doc.get("profile", {})
        if not isinstance(profile, Mapping):
            self.error("profile must be an object")
            return
        if profile.get("id") != "overflow.school-days-hq":
            self.error("profile.id must be overflow.school-days-hq")
        if profile.get("version") != "1.0.0":
            self.error("profile.version must be 1.0.0")
        metadata = profile.get("metadata", {})
        if isinstance(metadata, Mapping) and metadata.get("oracle_commit") != ORACLE_COMMIT:
            self.error("profile oracle_commit does not match frozen v1 oracle")

    def validate_counts(self) -> None:
        actual = {
            "nodes": len(self.records("nodes")),
            "transitions": len(self.records("transitions")),
            "choices": len(self.records("choices")),
            "endings": len(self.records("endings")),
        }
        for key in ("nodes", "transitions", "choices", "endings"):
            if actual[key] != EXPECTED[key]:
                self.error(f"{key} count mismatch: {actual[key]} != {EXPECTED[key]}")

        expected_resources = EXPECTED["nodes"] - len(ROUTING_ONLY)
        if len(self.records("resource_locators")) != expected_resources:
            self.error(
                f"resource locator count mismatch: {len(self.records('resource_locators'))} != {expected_resources}"
            )

    def validate_source_metadata(self) -> None:
        metadata = self.doc.get("metadata", {})
        if not isinstance(metadata, Mapping):
            self.error("top-level metadata missing")
            return

        source_oracle = metadata.get("source_oracle", {})
        if not isinstance(source_oracle, Mapping) or source_oracle.get("commit") != ORACLE_COMMIT:
            self.error("metadata.source_oracle.commit mismatch")

        counts = metadata.get("source_model_counts", {})
        if not isinstance(counts, Mapping):
            self.error("metadata.source_model_counts missing")
        else:
            for key, expected in EXPECTED.items():
                if counts.get(key) != expected:
                    self.error(f"source_model_counts.{key}: {counts.get(key)!r} != {expected}")

        offsets = metadata.get("source_route_offsets")
        if not isinstance(offsets, list) or len(offsets) != EXPECTED["routes"] + 1:
            self.error("source_route_offsets must contain 56 entries")
        elif offsets[0] != 0 or offsets[-1] != EXPECTED["nodes"]:
            self.error("source_route_offsets endpoints are invalid")

        excluded = metadata.get("excluded_structural_dead_transition_ids")
        if set(excluded or []) != EXCLUDED_STRUCTURAL_DEAD:
            self.error(f"excluded structural dead transition catalog mismatch: {excluded!r}")

        condition_indices: list[int] = []
        for row in self.records("expressions"):
            meta = row.get("metadata", {})
            if isinstance(meta, Mapping) and isinstance(meta.get("source_condition_index"), int):
                condition_indices.append(meta["source_condition_index"])
        if sorted(condition_indices) != list(range(EXPECTED["conditions"])):
            self.error(
                f"source condition coverage mismatch: {len(set(condition_indices))} unique / {EXPECTED['conditions']}"
            )

        source_effect_indices: list[int] = []
        feeling_delta_indices: list[int] = []
        for row in self.records("effects"):
            meta = row.get("metadata", {})
            if not isinstance(meta, Mapping):
                continue
            if isinstance(meta.get("source_effect_index"), int):
                source_effect_indices.append(meta["source_effect_index"])
            if isinstance(meta.get("source_feeling_delta_index"), int):
                feeling_delta_indices.append(meta["source_feeling_delta_index"])

        if sorted(source_effect_indices) != list(range(EXPECTED["effects"])):
            self.error(
                f"source effect coverage mismatch: {len(set(source_effect_indices))} unique / {EXPECTED['effects']}"
            )
        if sorted(feeling_delta_indices) != list(range(EXPECTED["feeling_deltas"])):
            self.error(
                f"feeling delta coverage mismatch: {len(set(feeling_delta_indices))} unique / {EXPECTED['feeling_deltas']}"
            )

    def validate_nodes_resources(self) -> None:
        routing_only_found: set[str] = set()
        coordinates: set[tuple[int, int]] = set()
        route_scenes: dict[int, set[int]] = {}

        for row in self.records("nodes"):
            meta = row.get("metadata", {})
            if not isinstance(meta, Mapping):
                self.error(f"node {row.get('id')} missing metadata")
                continue
            scene_key = meta.get("scene_key")
            route = meta.get("route")
            scene = meta.get("scene")
            if isinstance(route, int) and isinstance(scene, int):
                coord = (route, scene)
                if coord in coordinates:
                    self.error(f"duplicate route/scene coordinate {coord}")
                coordinates.add(coord)
                route_scenes.setdefault(route, set()).add(scene)
            resources = row.get("resources", [])
            if scene_key in ROUTING_ONLY:
                routing_only_found.add(str(scene_key))
                if row.get("kind") != "ktrf:dispatcher":
                    self.error(f"routing-only {scene_key} must be ktrf:dispatcher")
                if resources != []:
                    self.error(f"routing-only {scene_key} must not have physical resources")
            else:
                if row.get("kind") != "ktrf:scene":
                    self.error(f"ordinary School Days node {scene_key} must be ktrf:scene")
                if not isinstance(resources, list) or len(resources) != 1:
                    self.error(f"physical node {scene_key} must have exactly one scene-key locator")

        if routing_only_found != ROUTING_ONLY:
            self.error(f"routing-only node catalog mismatch: {sorted(routing_only_found)}")
        if len(coordinates) != EXPECTED["nodes"]:
            self.error(f"route/scene coordinate coverage mismatch: {len(coordinates)}")
        if set(route_scenes) != set(range(EXPECTED["routes"])):
            self.error("route coordinate catalog must cover routes 0..54")
        for route, scenes in route_scenes.items():
            if scenes and scenes != set(range(max(scenes) + 1)):
                self.error(f"route {route} scene coordinates are not contiguous from zero")

        for row in self.records("resource_locators"):
            if row.get("scheme") != "overflow.sdhq:scene-key":
                self.error(f"unexpected School Days locator scheme on {row.get('id')}")

    def validate_endings_hooks(self) -> None:
        codes = sorted(
            row.get("code")
            for row in self.records("endings")
            if isinstance(row.get("code"), int)
        )
        if codes != list(range(EXPECTED["endings"])):
            self.error(f"ending codes mismatch: {codes}")

        symbols = {row.get("symbol") for row in self.records("external_hooks")}
        if symbols != EXPECTED_HOOK_SYMBOLS:
            self.error(
                f"hook symbol catalog mismatch: {sorted(x for x in symbols if isinstance(x, str))}"
            )

    def validate_choice_resolution_coverage(self) -> None:
        resolution_indices: list[int] = []
        for choice in self.records("choices"):
            for option in choice.get("options", []):
                if not isinstance(option, Mapping):
                    continue
                meta = option.get("metadata", {})
                if isinstance(meta, Mapping) and isinstance(meta.get("source_feeling_resolution_index"), int):
                    resolution_indices.append(meta["source_feeling_resolution_index"])
            timeout = choice.get("timeout")
            if isinstance(timeout, Mapping):
                meta = timeout.get("metadata", {})
                if isinstance(meta, Mapping) and isinstance(meta.get("source_feeling_resolution_index"), int):
                    resolution_indices.append(meta["source_feeling_resolution_index"])

        if sorted(resolution_indices) != list(range(EXPECTED["feeling_resolutions"])):
            self.error(
                f"feeling resolution coverage mismatch: {len(set(resolution_indices))} unique / {EXPECTED['feeling_resolutions']}"
            )

    def validate_transition_lowering(self) -> None:
        effects = self.by_id("effects")
        expressions = self.by_id("expressions")
        transition_source_ids: set[int] = set()

        reset = effects.get(RESET_EFFECT_ID)
        if reset is None:
            self.error("profile reset-choice effect is missing")

        for row in self.records("transitions"):
            meta = row.get("metadata", {})
            if not isinstance(meta, Mapping):
                self.error(f"transition {row.get('id')} missing metadata")
                continue
            source_id = meta.get("source_transition_id")
            if not isinstance(source_id, int):
                self.error(f"transition {row.get('id')} missing source_transition_id")
                continue
            if source_id in transition_source_ids:
                self.error(f"duplicate source transition ID {source_id}")
            transition_source_ids.add(source_id)

            refs = row.get("effects", [])
            if not isinstance(refs, list):
                self.error(f"transition {row.get('id')} effects must be an array")
                continue

            start = meta.get("source_effect_start")
            count = meta.get("source_effect_count")
            if not isinstance(start, int) or not isinstance(count, int):
                self.error(f"transition {row.get('id')} missing source effect slice metadata")
                continue
            expected_source_refs = [
                f"sdhq:effect:source:{i}" for i in range(start, start + count)
            ]
            if refs[:count] != expected_source_refs:
                self.error(f"transition {row.get('id')} does not preserve source effect order")

            if len(refs) <= count or refs[count] != RESET_EFFECT_ID:
                self.error(
                    f"transition {row.get('id')} must reset choice result immediately after source effects"
                )

            synthetic = meta.get("profile_synthetic_coordinate_effects", [])
            if row.get("terminal") is True:
                if synthetic != []:
                    self.error(
                        f"terminal transition {row.get('id')} must not mirror a destination coordinate"
                    )
                if refs != expected_source_refs + [RESET_EFFECT_ID]:
                    self.error(
                        f"terminal transition {row.get('id')} contains unexpected post-source effects"
                    )
                if "destination" in row:
                    self.error(f"terminal transition {row.get('id')} has a destination")
            else:
                if not isinstance(synthetic, list) or len(synthetic) != 2:
                    self.error(
                        f"nonterminal transition {row.get('id')} must carry route/scene mirror effects"
                    )
                elif refs != expected_source_refs + [RESET_EFFECT_ID] + synthetic:
                    self.error(
                        f"nonterminal transition {row.get('id')} post-source effect order is incorrect"
                    )
                if "destination" not in row:
                    self.error(f"nonterminal transition {row.get('id')} has no destination")

            condition_count = meta.get("source_condition_count")
            condition_start = meta.get("source_condition_start")
            predicate = row.get("predicate")
            if condition_count == 0:
                if predicate is not None:
                    self.error(
                        f"unconditional transition {row.get('id')} unexpectedly has a predicate"
                    )
            elif condition_count == 1:
                expected_predicate = f"sdhq:expr:condition:{condition_start}"
                if predicate != expected_predicate:
                    self.error(f"single-condition transition {row.get('id')} predicate mismatch")
            elif isinstance(condition_count, int) and condition_count > 1:
                expr = expressions.get(str(predicate))
                if expr is None or expr.get("op") != "ktrf:and":
                    self.error(
                        f"multi-condition transition {row.get('id')} must use ktrf:and conjunction"
                    )
                else:
                    expected_args = [
                        {"kind": "expression", "ref": f"sdhq:expr:condition:{i}"}
                        for i in range(condition_start, condition_start + condition_count)
                    ]
                    if expr.get("args") != expected_args:
                        self.error(
                            f"multi-condition transition {row.get('id')} conjunction order mismatch"
                        )

        if len(transition_source_ids) != EXPECTED["transitions"]:
            self.error(f"source transition coverage mismatch: {len(transition_source_ids)}")

        # Critical provenance rule: the six known raw structural dead branches
        # must remain absent from the frozen executable-oracle IR. Reintroducing
        # them would silently change the 2,458-transition oracle.
        overlap = transition_source_ids & EXCLUDED_STRUCTURAL_DEAD
        if overlap:
            self.error(
                f"excluded structural dead transitions were reintroduced: {sorted(overlap)}"
            )

    def validate_callback38(self) -> None:
        hooks = self.by_id("external_hooks")
        callback38_hook_ids = {
            hook_id
            for hook_id, row in hooks.items()
            if row.get("symbol") == "overflow.sdhq:callback_38"
        }
        if len(callback38_hook_ids) != 1:
            self.error("expected exactly one callback_38 ExternalHook")
            return
        callback38_hook = next(iter(callback38_hook_ids))

        callback38_effect_ids: set[str] = set()
        for effect_id, row in self.by_id("effects").items():
            if row.get("op") != "ktrf:call-hook":
                continue
            args = row.get("args", {})
            if isinstance(args, Mapping) and args.get("hook") == callback38_hook:
                callback38_effect_ids.add(effect_id)

        terminal = 0
        nonterminal = 0
        for row in self.records("transitions"):
            refs = row.get("effects", [])
            if not isinstance(refs, list) or not any(
                ref in callback38_effect_ids for ref in refs
            ):
                continue
            if row.get("terminal") is True:
                terminal += 1
            else:
                nonterminal += 1

        if terminal != EXPECTED["callback38_terminal"]:
            self.error(f"terminal callback_38 edge count mismatch: {terminal}")
        if nonterminal != EXPECTED["callback38_nonterminal"]:
            self.error(f"nonterminal callback_38 edge count mismatch: {nonterminal}")

    def validate_entry_and_defaults(self) -> None:
        entries = self.records("entry_points")
        expected_entry = next(
            (x for x in entries if x.get("id") == "sdhq:entry:new-game"), None
        )
        if expected_entry is None:
            self.error("New Game entry point is missing")
        elif expected_entry.get("node") != "sdhq:node:00/00-00-A00":
            self.error("New Game entry point must target 00/00-00-A00")

        variables = self.by_id("variables")
        expected_defaults = {
            "sdhq:var:internal:choice_result": -2,
            "sdhq:var:internal:callback34": 0,
            "sdhq:var:global:dword_3A6F40": 0,
            "sdhq:var:global:dword_3A2294": 1,
        }
        for var_id, expected in expected_defaults.items():
            row = variables.get(var_id)
            if row is None:
                self.error(f"required profile variable missing: {var_id}")
            elif row.get("default") != expected:
                self.error(
                    f"default mismatch for {var_id}: {row.get('default')!r} != {expected}"
                )


def validate_profile(document: Mapping[str, Any]) -> list[str]:
    return ProfileValidator(document).validate()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate School Days HQ KTRF profile conformance"
    )
    parser.add_argument("input", type=Path)
    args = parser.parse_args()

    path = args.input.resolve()
    document = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(document, Mapping):
        print("SDHQ KTRF PROFILE INVALID: root must be an object")
        return 2

    errors = validate_profile(document)
    if errors:
        for error in errors:
            print(f"ERROR SDHQ {error}")
        print(f"SDHQ KTRF PROFILE INVALID: {len(errors)} error(s) — {path}")
        return 1

    print(f"SDHQ KTRF PROFILE PASS: 0 errors — {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
