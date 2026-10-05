#!/usr/bin/env python3

from __future__ import annotations

import copy
import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "ktrf"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import binary_semantic_phase7_v0_1 as phase7  # noqa: E402


EXAMPLE = ROOT / "examples" / "ktrf" / "minimal-routing-ir.json"


def load_example() -> dict:
    return json.loads(EXAMPLE.read_text(encoding="utf-8"))


class Phase7TransitionTests(unittest.TestCase):
    def roundtrip(self, doc: dict) -> dict:
        data = phase7.build_phase7_container(doc)
        decoded = phase7.parse_phase7_container(data)
        self.assertEqual(decoded, phase7.canonical_phase7_projection(doc))
        return decoded

    def test_minimal_roundtrip(self) -> None:
        decoded = self.roundtrip(load_example())
        transition = decoded["transitions"][0]
        self.assertEqual(transition["id"], "example:transition:end")
        self.assertTrue(transition["terminal"])
        self.assertNotIn("destination", transition)

    def test_canonical_section_order(self) -> None:
        data = phase7.build_phase7_container(load_example())
        parsed = phase7.container.parse_container(
            data, known_section_types=phase7.PHASE7_SECTION_TYPES
        )
        self.assertEqual(
            [entry.type_code for entry in parsed.entries],
            list(phase7.PHASE7_SECTION_TYPES),
        )

    def test_transition_indices_are_canonical(self) -> None:
        doc = load_example()
        base = copy.deepcopy(doc["transitions"][0])
        z = copy.deepcopy(base)
        z["id"] = "example:transition:z"
        a = copy.deepcopy(base)
        a["id"] = "example:transition:a"
        doc["transitions"] = [z, a]
        self.assertEqual(
            phase7.transition_index_map(doc),
            {"example:transition:a": 0, "example:transition:z": 1},
        )

    def test_transition_source_order_does_not_change_binary(self) -> None:
        doc = load_example()
        second = copy.deepcopy(doc["transitions"][0])
        second["id"] = "example:transition:second"
        second["priority"] = 1
        doc["transitions"].append(second)
        first = phase7.build_phase7_container(doc)
        doc["transitions"].reverse()
        again = phase7.build_phase7_container(doc)
        self.assertEqual(first, again)

    def test_effect_order_is_preserved(self) -> None:
        doc = load_example()
        second = copy.deepcopy(doc["effects"][0])
        second["id"] = "example:effect:register_ending_1_b"
        doc["effects"].append(second)
        doc["transitions"][0]["effects"] = [
            "example:effect:register_ending_1_b",
            "example:effect:register_ending_1",
        ]
        decoded = self.roundtrip(doc)
        self.assertEqual(
            decoded["transitions"][0]["effects"],
            [
                "example:effect:register_ending_1_b",
                "example:effect:register_ending_1",
            ],
        )

    def test_trigger_order_is_preserved(self) -> None:
        doc = load_example()
        doc["transitions"][0]["triggers"] = ["ktrf:next", "example:resume"]
        decoded = self.roundtrip(doc)
        self.assertEqual(
            decoded["transitions"][0]["triggers"],
            ["ktrf:next", "example:resume"],
        )

    def test_absent_triggers_roundtrip_as_absent(self) -> None:
        doc = load_example()
        del doc["transitions"][0]["triggers"]
        decoded = self.roundtrip(doc)
        self.assertNotIn("triggers", decoded["transitions"][0])

    def test_nonterminal_destination_is_physically_node_indexed(self) -> None:
        doc = load_example()
        doc["nodes"].append(
            {"id": "example:node:z", "kind": "ktrf:scene", "resources": []}
        )
        transition = doc["transitions"][0]
        transition["terminal"] = False
        transition["destination"] = "example:node:z"

        data = phase7.build_phase7_container(doc)
        parsed = phase7.container.parse_container(
            data, known_section_types=phase7.PHASE7_SECTION_TYPES
        )
        payload = parsed.sections[b"TRAN"]
        record_offset = phase7.TRAN_HEADER_SIZE
        values = phase7.TRAN_RECORD_STRUCT.unpack_from(payload, record_offset)
        source_i, destination_i = values[1], values[2]
        nodes = phase7.phase6.node_index_map(doc)
        self.assertEqual(source_i, nodes["example:node:start"])
        self.assertEqual(destination_i, nodes["example:node:z"])

        decoded = phase7.parse_phase7_container(data)
        self.assertEqual(
            decoded["transitions"][0]["destination"], "example:node:z"
        )

    def test_unknown_source_is_rejected(self) -> None:
        doc = load_example()
        doc["transitions"][0]["source"] = "example:node:missing"
        with self.assertRaisesRegex(phase7.KtrfPhase7Error, "unknown source node"):
            phase7.build_phase7_container(doc)

    def test_unknown_destination_is_rejected(self) -> None:
        doc = load_example()
        transition = doc["transitions"][0]
        transition["terminal"] = False
        transition["destination"] = "example:node:missing"
        with self.assertRaisesRegex(phase7.KtrfPhase7Error, "unknown destination node"):
            phase7.build_phase7_container(doc)

    def test_unknown_predicate_is_rejected(self) -> None:
        doc = load_example()
        doc["transitions"][0]["predicate"] = "example:expr:missing"
        with self.assertRaisesRegex(phase7.KtrfPhase7Error, "unknown predicate"):
            phase7.build_phase7_container(doc)

    def test_unknown_effect_is_rejected(self) -> None:
        doc = load_example()
        doc["transitions"][0]["effects"] = ["example:effect:missing"]
        with self.assertRaisesRegex(phase7.KtrfPhase7Error, "unknown effect"):
            phase7.build_phase7_container(doc)

    def test_terminal_destination_contract_is_strict(self) -> None:
        doc = load_example()
        doc["transitions"][0]["destination"] = "example:node:start"
        with self.assertRaisesRegex(phase7.KtrfPhase7Error, "must not have destination"):
            phase7.build_phase7_container(doc)

        doc = load_example()
        doc["transitions"][0]["terminal"] = False
        with self.assertRaisesRegex(phase7.KtrfPhase7Error, "requires destination"):
            phase7.build_phase7_container(doc)

    def test_priority_must_fit_u32(self) -> None:
        doc = load_example()
        doc["transitions"][0]["priority"] = 0x1_0000_0000
        with self.assertRaisesRegex(phase7.KtrfPhase7Error, "out of u32 range"):
            phase7.build_phase7_container(doc)

    def test_duplicate_trigger_is_rejected(self) -> None:
        doc = load_example()
        doc["transitions"][0]["triggers"] = ["ktrf:next", "ktrf:next"]
        with self.assertRaisesRegex(phase7.KtrfPhase7Error, "duplicate trigger"):
            phase7.build_phase7_container(doc)

    def test_duplicate_transition_id_is_rejected(self) -> None:
        doc = load_example()
        doc["transitions"].append(copy.deepcopy(doc["transitions"][0]))
        with self.assertRaisesRegex(phase7.KtrfPhase7Error, "duplicate Transition id"):
            phase7.build_phase7_container(doc)


if __name__ == "__main__":
    unittest.main(verbosity=2)
