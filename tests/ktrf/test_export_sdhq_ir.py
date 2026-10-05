#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EXPORTER_PATH = ROOT / "tools" / "ktrf" / "export_sdhq_to_ir.py"
VALIDATOR_PATH = ROOT / "tools" / "ktrf" / "validate_ir.py"
PROFILE_VALIDATOR_PATH = ROOT / "tools" / "ktrf" / "validate_sdhq_ir.py"
GENERATED_PATH = ROOT / "src" / "SchoolDaysRouteData.generated.inc"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


exporter = load_module("ktrf_export_sdhq", EXPORTER_PATH)
semantic = load_module("ktrf_validate_ir_for_sdhq", VALIDATOR_PATH)
profile = load_module("ktrf_validate_sdhq", PROFILE_VALIDATOR_PATH)


class SchoolDaysKtrfExporterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.model = exporter.parse_generated(GENERATED_PATH)
        cls.doc = exporter.build_document(cls.model, GENERATED_PATH)

    def test_frozen_source_counts(self) -> None:
        self.assertEqual(55, len(self.model.route_offsets) - 1)
        self.assertEqual(1857, len(self.model.nodes))
        self.assertEqual(2458, len(self.model.transitions))
        self.assertEqual(1464, len(self.model.conditions))
        self.assertEqual(6183, len(self.model.effects))
        self.assertEqual(302, len(self.model.feeling_deltas))
        self.assertEqual(772, len(self.model.feeling_resolutions))

    def test_ir_inventory(self) -> None:
        self.assertEqual(1857, len(self.doc["nodes"]))
        self.assertEqual(2458, len(self.doc["transitions"]))
        self.assertEqual(287, len(self.doc["choices"]))
        self.assertEqual(22, len(self.doc["endings"]))
        self.assertEqual(1855, len(self.doc["resource_locators"]))

    def test_generic_semantic_validation_passes(self) -> None:
        diagnostics = semantic.Validator(self.doc).validate()
        errors = [d for d in diagnostics if d.severity == "error"]
        self.assertEqual([], errors)

    def test_school_days_profile_validation_passes(self) -> None:
        self.assertEqual([], profile.validate_profile(self.doc))

    def test_routing_only_nodes_have_no_physical_locator(self) -> None:
        by_scene = {row["metadata"]["scene_key"]: row for row in self.doc["nodes"]}
        for scene in ("03/03-B2-A00", "03/03-KB-E00"):
            row = by_scene[scene]
            self.assertEqual("ktrf:dispatcher", row["kind"])
            self.assertEqual([], row["resources"])

    def test_entry_point_is_frozen_new_game(self) -> None:
        entry = self.doc["entry_points"][0]
        self.assertEqual("sdhq:entry:new-game", entry["id"])
        self.assertEqual("sdhq:node:00/00-00-A00", entry["node"])
        self.assertEqual("ktrf:new-game", entry["trigger"])

    def test_structural_dead_catalog_is_recorded_as_excluded_provenance(self) -> None:
        excluded = set(
            self.doc["metadata"]["excluded_structural_dead_transition_ids"]
        )
        expected = {971, 1243, 1248, 1494, 1824, 1825}
        self.assertEqual(expected, excluded)

        source_ids = {
            row["metadata"]["source_transition_id"]
            for row in self.doc["transitions"]
        }
        self.assertTrue(expected.isdisjoint(source_ids))

    def test_transition_t25_preserves_handoff_hooks(self) -> None:
        effects = {row["id"]: row for row in self.doc["effects"]}
        hooks = {row["id"]: row["symbol"] for row in self.doc["external_hooks"]}
        t25 = next(
            row for row in self.doc["transitions"]
            if row["metadata"]["source_transition_id"] == 25
        )
        self.assertEqual("sdhq:node:00/00-00-L00", t25["source"])
        self.assertEqual("sdhq:node:01/01-00-A00", t25["destination"])
        called = []
        for ref in t25["effects"]:
            effect = effects[ref]
            if effect["op"] == "ktrf:call-hook":
                called.append(hooks[effect["args"]["hook"]])
        self.assertEqual(
            [
                "overflow.sdhq:callback_00",
                "overflow.sdhq:callback_2C",
                "overflow.sdhq:callback_38",
            ],
            called,
        )

    def test_transition_t2357_preserves_terminal_semantics(self) -> None:
        effects = {row["id"]: row for row in self.doc["effects"]}
        t2357 = next(
            row for row in self.doc["transitions"]
            if row["metadata"]["source_transition_id"] == 2357
        )
        self.assertTrue(t2357["terminal"])
        self.assertNotIn("destination", t2357)
        ops = [effects[ref]["op"] for ref in t2357["effects"]]
        self.assertIn("ktrf:register-ending", ops)
        self.assertIn("ktrf:call-hook", ops)
        self.assertEqual("ktrf:set", effects[t2357["effects"][-1]]["op"])
        self.assertEqual(
            "sdhq:var:internal:choice_result",
            effects[t2357["effects"][-1]]["args"]["target"],
        )

    def test_build_is_deterministic(self) -> None:
        again = exporter.build_document(self.model, GENERATED_PATH)
        left = json.dumps(
            self.doc,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        )
        right = json.dumps(
            again,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        )
        self.assertEqual(left, right)


if __name__ == "__main__":
    unittest.main(verbosity=2)
