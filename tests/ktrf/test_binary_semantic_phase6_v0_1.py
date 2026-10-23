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

import binary_semantic_phase6_v0_1 as phase6  # noqa: E402


EXAMPLE = ROOT / "examples" / "ktrf" / "minimal-routing-ir.json"


def load_example() -> dict:
    return json.loads(EXAMPLE.read_text(encoding="utf-8"))


class Phase6NodeTests(unittest.TestCase):
    def roundtrip(self, doc: dict) -> dict:
        data = phase6.build_phase6_container(doc)
        decoded = phase6.parse_phase6_container(data)
        self.assertEqual(decoded, phase6.canonical_phase6_projection(doc))
        return decoded

    def test_minimal_roundtrip(self) -> None:
        decoded = self.roundtrip(load_example())
        self.assertEqual(decoded["nodes"][0]["id"], "example:node:start")
        self.assertEqual(decoded["entry_points"][0]["node"], "example:node:start")

    def test_canonical_section_order(self) -> None:
        data = phase6.build_phase6_container(load_example())
        parsed = phase6.container.parse_container(
            data, known_section_types=phase6.PHASE6_SECTION_TYPES
        )
        self.assertEqual(
            [entry.type_code for entry in parsed.entries],
            list(phase6.PHASE6_SECTION_TYPES),
        )

    def test_node_indices_are_canonical(self) -> None:
        doc = load_example()
        doc["nodes"] = [
            {"id": "example:node:z", "kind": "ktrf:scene", "resources": []},
            {"id": "example:node:a", "kind": "ktrf:dispatcher", "resources": []},
        ]
        doc["entry_points"][0]["node"] = "example:node:z"
        self.assertEqual(
            phase6.node_index_map(doc),
            {"example:node:a": 0, "example:node:z": 1},
        )

        data = phase6.build_phase6_container(doc)
        parsed = phase6.container.parse_container(
            data, known_section_types=phase6.PHASE6_SECTION_TYPES
        )
        _, node_index, _ = phase6.phase1.ENTRY_STRUCT.unpack(
            parsed.sections[b"ENTR"]
        )
        self.assertEqual(node_index, 1)

    def test_node_source_order_does_not_change_binary(self) -> None:
        doc = load_example()
        doc["nodes"] = [
            {"id": "example:node:b", "kind": "ktrf:scene", "resources": []},
            {"id": "example:node:a", "kind": "ktrf:scene", "resources": []},
        ]
        doc["entry_points"][0]["node"] = "example:node:a"
        first = phase6.build_phase6_container(doc)
        doc["nodes"].reverse()
        second = phase6.build_phase6_container(doc)
        self.assertEqual(first, second)

    def test_resource_reference_order_is_preserved(self) -> None:
        doc = load_example()
        doc["resource_locators"] = [
            {"id": "example:resource:a", "scheme": "example:path", "value": "a"},
            {"id": "example:resource:b", "scheme": "example:path", "value": "b"},
        ]
        doc["nodes"][0]["resources"] = [
            "example:resource:b",
            "example:resource:a",
        ]
        decoded = self.roundtrip(doc)
        self.assertEqual(
            decoded["nodes"][0]["resources"],
            ["example:resource:b", "example:resource:a"],
        )

    def test_unknown_resource_is_rejected(self) -> None:
        doc = load_example()
        doc["nodes"][0]["resources"] = ["example:resource:missing"]
        with self.assertRaisesRegex(phase6.KtrfPhase6Error, "unknown resource"):
            phase6.build_phase6_container(doc)

    def test_unknown_entry_node_is_rejected(self) -> None:
        doc = load_example()
        doc["entry_points"][0]["node"] = "example:node:missing"
        with self.assertRaisesRegex(phase6.KtrfPhase6Error, "unknown node"):
            phase6.build_phase6_container(doc)

    def test_unknown_choice_node_is_rejected(self) -> None:
        doc = load_example()
        doc["choices"] = [
            {
                "id": "example:choice:one",
                "node": "example:node:missing",
                "result_variable": "example:var:flag",
                "routing_policy": "ktrf:deferred",
                "options": [
                    {"id": "one", "value": 1, "effects": []},
                ],
            }
        ]
        with self.assertRaisesRegex(phase6.KtrfPhase6Error, "unknown node"):
            phase6.build_phase6_container(doc)

    def test_duplicate_node_id_is_rejected(self) -> None:
        doc = load_example()
        doc["nodes"].append(copy.deepcopy(doc["nodes"][0]))
        with self.assertRaisesRegex(phase6.KtrfPhase6Error, "duplicate Node id"):
            phase6.build_phase6_container(doc)

    def test_node_label_roundtrip(self) -> None:
        doc = load_example()
        doc["nodes"][0]["label"] = "Start"
        decoded = self.roundtrip(doc)
        self.assertEqual(decoded["nodes"][0]["label"], "Start")

    def test_choice_node_is_physically_lowered_to_node_index(self) -> None:
        doc = load_example()
        doc["nodes"] = [
            {"id": "example:node:z", "kind": "ktrf:scene", "resources": []},
            {"id": "example:node:a", "kind": "ktrf:scene", "resources": []},
        ]
        doc["entry_points"][0]["node"] = "example:node:a"
        doc["choices"] = [
            {
                "id": "example:choice:one",
                "node": "example:node:z",
                "result_variable": "example:var:flag",
                "routing_policy": "ktrf:deferred",
                "options": [
                    {"id": "one", "value": 1, "effects": []},
                ],
            }
        ]
        data = phase6.build_phase6_container(doc)
        parsed = phase6.container.parse_container(
            data, known_section_types=phase6.PHASE6_SECTION_TYPES
        )
        header = phase6.phase5.CHOI_HEADER_STRUCT.unpack_from(
            parsed.sections[b"CHOI"], 0
        )
        choices_offset = header[8]
        (_, node_index, *_rest) = phase6.phase5.CHOI_RECORD_STRUCT.unpack_from(
            parsed.sections[b"CHOI"], choices_offset
        )
        self.assertEqual(node_index, 1)

        decoded = phase6.parse_phase6_container(data)
        self.assertEqual(decoded["choices"][0]["node"], "example:node:z")


if __name__ == "__main__":
    unittest.main(verbosity=2)
