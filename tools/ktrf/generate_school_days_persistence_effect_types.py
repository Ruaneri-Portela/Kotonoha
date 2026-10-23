#!/usr/bin/env python3
"""Generate the School Days session-persistence FlgH type oracle.

The oracle is pinned to the current authenticated/generated School Days route
evidence.  It classifies only KTRF effects that write persisted session numeric
state.  Effects with no persisted-session write are intentionally omitted from
the compact output; any session write outside the closed rules below fails the
generation instead of being guessed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
from dataclasses import dataclass


EXPECTED_ROUTING_SHA256 = "9F04062B3BC7F5F270374F3B525A8D6C05EB3C647C1C7999D099EC0A82B5D7A7"
EXPECTED_ROUTE_SOURCE_SHA256 = "031B53C172BB37E87A41356A8C17D84FD173606C7D0EA509607578A4629F921B"
EXPECTED_ORACLE_COMMIT = "614461c2b14951ba117b9d2dedb4983cfa8ae8e6"
EXPECTED_COUNTS = {
    "effects": 11356,
    "source_effects": 6183,
    "feeling_effects": 302,
    "profile_effects": 4871,
    "persistent_entries": 11123,
    "type3": 11014,
    "type11": 109,
    "no_persistence": 233,
}
EXPECTED_PAYLOAD_SHA256 = "9424B5BA025C635FA441487A5B8266D0F174491AED9ECD00DD3C9FD8AF8EBDCA"
EXPECTED_GLOBAL_PAYLOAD_SHA256 = "D12AE1B839CB68B12B200FA576ECED886A85F4DE1B872CBE2FE7D7C49DE8EE97"


@dataclass(frozen=True)
class Entry:
    effect_index: int
    target: str
    flgh_type: int
    source_effect_index: int


@dataclass(frozen=True)
class GlobalEntry:
    source_effect_index: int
    target: str
    flgh_type: int


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def fail(message: str) -> None:
    raise RuntimeError(message)


def cpp_string(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def source_symbol(variable: dict) -> str | None:
    return variable.get("metadata", {}).get("source_symbol")


def source_storage(variable: dict) -> str | None:
    return variable.get("metadata", {}).get("source_storage")


def validate_witnesses(document: dict) -> None:
    effects = document["effects"]
    expected = {
        197: ("999", 1, 1, 37),
        201: ("998", 1, 1, 38),
    }
    for effect_index, (target, value, route, scene) in expected.items():
        effect = effects[effect_index]
        metadata = effect.get("metadata", {})
        if (
            effect.get("id") != f"sdhq:effect:source:{effect_index}"
            or metadata.get("source_kind") != "SetSessionConst"
            or metadata.get("source_target") != target
            or metadata.get("source_value") != value
        ):
            fail(f"authenticated numeric-bool witness drift at effect {effect_index}")
        effect_id = effect["id"]
        matching = [
            t
            for t in document["transitions"]
            if t.get("metadata", {}).get("source_route") == route
            and t.get("metadata", {}).get("source_scene") == scene
            and effect_id in t.get("effects", [])
        ]
        if len(matching) != 1:
            fail(
                f"authenticated numeric-bool witness {target} is not uniquely used "
                f"at route {route} scene {scene}"
            )


def build_entries(document: dict) -> tuple[list[Entry], list[str], dict[str, int]]:
    variables = {v["id"]: v for v in document["variables"]}
    entries: list[Entry] = []
    unresolved: list[str] = []
    no_persistence = 0
    source_count = 0
    feeling_count = 0
    profile_count = 0

    for effect_index, effect in enumerate(document["effects"]):
        metadata = effect.get("metadata", {})
        args = effect.get("args", {})
        effect_id = effect.get("id")
        target_id = args.get("target")
        target_var = variables.get(target_id) if isinstance(target_id, str) else None
        target = source_symbol(target_var) if target_var is not None else None
        storage = source_storage(target_var) if target_var is not None else None
        source_index = metadata.get("source_effect_index", -1)
        flgh_type: int | None = None

        if "source_effect_index" in metadata:
            source_count += 1
            if source_index != effect_index or effect_id != f"sdhq:effect:source:{effect_index}":
                unresolved.append(f"effect {effect_index}: source index/id mismatch")
                no_persistence += 1
                continue

        kind = metadata.get("source_kind")
        if kind == "SetSessionConst":
            if effect.get("op") != "ktrf:set" or storage != "session":
                unresolved.append(f"effect {effect_index}: malformed SetSessionConst target")
            elif target != metadata.get("source_target"):
                unresolved.append(f"effect {effect_index}: target metadata mismatch")
            elif args.get("value") != {"kind": "literal", "value": metadata.get("source_value")}:
                unresolved.append(f"effect {effect_index}: SetSessionConst value mismatch")
            elif target in ("ROUTE", "SCENE") or (target is not None and target.startswith("BS")):
                # Authenticated session integer setter: interface +0x0C -> FlgH type 3.
                flgh_type = 3
            elif target is not None and target.isdigit() and metadata.get("source_value") == 1:
                # Authenticated numeric-flag setter: interface +0x14 -> FlgH type 11 bool.
                flgh_type = 11
            else:
                unresolved.append(
                    f"effect {effect_index}: unclassified SetSessionConst "
                    f"target={target!r} value={metadata.get('source_value')!r}"
                )
        elif kind == "SetSessionFromSession":
            source_id = args.get("source")
            source_var = variables.get(source_id) if isinstance(source_id, str) else None
            if (
                effect.get("op") != "ktrf:copy"
                or storage != "session"
                or source_storage(source_var) != "session"
                or target != metadata.get("source_target")
                or source_symbol(source_var) != metadata.get("source_source")
            ):
                unresolved.append(f"effect {effect_index}: malformed SetSessionFromSession")
            else:
                # Authenticated copy path writes the destination through integer +0x0C.
                flgh_type = 3
        elif "source_feeling_delta_index" in metadata:
            feeling_count += 1
            if (
                effect.get("op") != "ktrf:add"
                or storage != "session"
                or target not in {"000", "001", "002", "003", "004"}
                or target != metadata.get("source_variable")
            ):
                unresolved.append(f"effect {effect_index}: malformed feeling write")
            else:
                # RouteProc 0x10005CE0 reads via +0x08 and writes current+delta via +0x0C.
                flgh_type = 3
                source_index = -1
        elif metadata.get("profile_synthetic") is True:
            profile_count += 1
            source_index = -1
            if storage == "router-internal":
                # choice_result/callback34 are router bookkeeping, absent from FlgH persistence.
                flgh_type = None
            elif (
                effect.get("op") == "ktrf:set"
                and storage == "session"
                and target in ("ROUTE", "SCENE")
            ):
                # Synthetic destination-coordinate mirrors have the same integer storage type.
                flgh_type = 3
            else:
                unresolved.append(f"effect {effect_index}: unclassified profile synthetic write")

        if flgh_type is not None:
            if target is None:
                unresolved.append(f"effect {effect_index}: persistent write has no target symbol")
            else:
                entries.append(Entry(effect_index, target, flgh_type, int(source_index)))
        else:
            no_persistence += 1

    counts = {
        "effects": len(document["effects"]),
        "source_effects": source_count,
        "feeling_effects": feeling_count,
        "profile_effects": profile_count,
        "persistent_entries": len(entries),
        "type3": sum(e.flgh_type == 3 for e in entries),
        "type11": sum(e.flgh_type == 11 for e in entries),
        "no_persistence": no_persistence,
    }
    return entries, unresolved, counts


def payload_sha256(entries: list[Entry]) -> str:
    payload = "".join(
        f"{e.effect_index}\t{e.target}\t{e.flgh_type}\t{e.source_effect_index}\n"
        for e in entries
    ).encode("utf-8")
    return sha256_bytes(payload)


def build_global_entries(document: dict) -> list[GlobalEntry]:
    variables = {v["id"]: v for v in document["variables"]}
    rows: list[GlobalEntry] = []
    for effect_index, effect in enumerate(document["effects"]):
        metadata = effect.get("metadata", {})
        if metadata.get("source_kind") != "SetGlobalConst":
            continue
        args = effect.get("args", {})
        target_var = variables.get(args.get("target"))
        if (
            target_var is None
            or source_storage(target_var) != "global"
            or effect.get("id") != f"sdhq:effect:source:{effect_index}"
            or effect.get("op") != "ktrf:set"
            or metadata.get("source_value") != 1
            or args.get("value") != {"kind": "literal", "value": 1}
        ):
            fail(f"malformed authenticated SetGlobalConst effect {effect_index}")
        target = source_symbol(target_var)
        if target is None or not target.startswith("REP"):
            fail(f"unexpected global persistence target at effect {effect_index}: {target!r}")
        # Authenticated GlobalFlag snapshot stores REP* as VT_BOOL/type11.
        rows.append(GlobalEntry(effect_index, target, 11))
    if len(rows) != 67 or len({row.target for row in rows}) != 49:
        fail(
            "global persistence write-count drift: "
            f"writes={len(rows)} targets={len({row.target for row in rows})}"
        )
    return rows


def global_payload_sha256(entries: list[GlobalEntry]) -> str:
    payload = "".join(
        f"{e.source_effect_index}\t{e.target}\t{e.flgh_type}\n"
        for e in entries
    ).encode("utf-8")
    return sha256_bytes(payload)


def render(
    entries: list[Entry],
    global_entries: list[GlobalEntry],
    counts: dict[str, int],
    routing_sha: str,
    route_source_sha: str,
    payload_sha: str,
) -> str:
    out = [
        "// GENERATED FILE. DO NOT EDIT BY HAND.\n",
        "// Generated by tools/ktrf/generate_school_days_persistence_effect_types.py.\n",
        "// Authenticated setter semantics: FlgH type 3 = session integer (+0x0C);\n",
        "// FlgH type 11 = session bool (+0x14). Synthetic source_effect_index is -1.\n",
        f"// routing.json SHA256 {routing_sha}\n",
        f"// SchoolDaysRouteData.generated.inc SHA256 {route_source_sha}\n",
        f"// oracle payload SHA256 {payload_sha}\n",
        f"// effects={counts['effects']} persistent={counts['persistent_entries']} "
        f"type3={counts['type3']} type11={counts['type11']} "
        f"no_persistence={counts['no_persistence']}\n\n",
        "struct SchoolDaysPersistenceEffectTypeEntry {\n",
        "    unsigned int effect_index;\n",
        "    const char* target;\n",
        "    unsigned char type;\n",
        "    int source_effect_index;\n",
        "};\n\n",
        "static constexpr SchoolDaysPersistenceEffectTypeEntry "
        "kSchoolDaysPersistenceEffectTypes[] = {\n",
    ]
    for entry in entries:
        out.append(
            f'    {{{entry.effect_index}u, "{cpp_string(entry.target)}", '
            f'{entry.flgh_type}u, {entry.source_effect_index}}},\n'
        )
    out.extend(
        [
            "};\n\n",
        "struct SchoolDaysGlobalPersistenceEffectEntry {\n",
        "    unsigned int source_effect_index;\n",
        "    const char* target;\n",
        "    unsigned char type;\n",
        "};\n\n",
        "static constexpr SchoolDaysGlobalPersistenceEffectEntry "
        "kSchoolDaysGlobalPersistenceEffects[] = {\n",
    ]
    )
    for entry in global_entries:
        out.append(
            f'    {{{entry.source_effect_index}u, "{cpp_string(entry.target)}", '
            f'{entry.flgh_type}u}},\n'
        )
    out.extend(
        [
            "};\n\n",
            f"static constexpr unsigned int kSchoolDaysGlobalPersistenceEffectCount = "
            f"{len(global_entries)}u;\n",
            f'static constexpr const char kSchoolDaysGlobalPersistenceEffectsSha256[] = "{global_payload_sha256(global_entries)}";\n',
            f"static constexpr unsigned int kSchoolDaysPersistenceEffectCount = "
            f"{counts['effects']}u;\n",
            f"static constexpr unsigned int kSchoolDaysPersistenceEffectTypeCount = "
            f"{counts['persistent_entries']}u;\n",
            f"static constexpr unsigned int kSchoolDaysPersistenceEffectType3Count = "
            f"{counts['type3']}u;\n",
            f"static constexpr unsigned int kSchoolDaysPersistenceEffectType11Count = "
            f"{counts['type11']}u;\n",
            f"static constexpr unsigned int kSchoolDaysPersistenceNoPersistenceEffectCount = "
            f"{counts['no_persistence']}u;\n",
            f'static constexpr const char kSchoolDaysPersistenceRoutingSha256[] = "{routing_sha}";\n',
            f'static constexpr const char kSchoolDaysPersistenceRouteSourceSha256[] = "{route_source_sha}";\n',
            f'static constexpr const char kSchoolDaysPersistenceEffectTypesSha256[] = "{payload_sha}";\n',
        ]
    )
    return "".join(out)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--routing-json",
        type=pathlib.Path,
        default=pathlib.Path("build/ktrf/school-days-hq.routing.json"),
    )
    parser.add_argument(
        "--route-source",
        type=pathlib.Path,
        default=pathlib.Path("games/school-days/src/SchoolDaysRouteData.generated.inc"),
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=pathlib.Path("games/school-days/src/generated/SchoolDaysPersistenceEffectTypes.inc"),
    )
    args = parser.parse_args()

    routing_bytes = args.routing_json.read_bytes()
    routing_sha = sha256_bytes(routing_bytes)
    if routing_sha != EXPECTED_ROUTING_SHA256:
        fail(f"routing JSON hash drift: got {routing_sha}, expected {EXPECTED_ROUTING_SHA256}")
    document = json.loads(routing_bytes.decode("utf-8"))
    if document.get("format") != "ktrf-routing-ir" or document.get("ir_version") != "0.1.0":
        fail("unexpected routing document format/version")

    oracle = document.get("metadata", {}).get("source_oracle", {})
    if oracle.get("commit") != EXPECTED_ORACLE_COMMIT:
        fail(f"source oracle commit drift: {oracle.get('commit')!r}")
    route_source_bytes = args.route_source.read_bytes()
    route_source_sha = sha256_bytes(route_source_bytes)
    declared_route_sha = str(oracle.get("generated_table_sha256", "")).upper()
    if route_source_sha != EXPECTED_ROUTE_SOURCE_SHA256 or route_source_sha != declared_route_sha:
        fail(
            "generated route source hash drift: "
            f"actual={route_source_sha} metadata={declared_route_sha} "
            f"expected={EXPECTED_ROUTE_SOURCE_SHA256}"
        )

    model_counts = document.get("metadata", {}).get("source_model_counts", {})
    if model_counts.get("effects") != EXPECTED_COUNTS["source_effects"]:
        fail(f"source model effect-count drift: {model_counts.get('effects')!r}")
    if model_counts.get("feeling_deltas") != EXPECTED_COUNTS["feeling_effects"]:
        fail(f"source model feeling-count drift: {model_counts.get('feeling_deltas')!r}")

    validate_witnesses(document)
    entries, unresolved, counts = build_entries(document)
    if unresolved:
        detail = "\n".join(unresolved[:32])
        fail(f"unresolved session persistence effects ({len(unresolved)}):\n{detail}")
    if counts != EXPECTED_COUNTS:
        fail(f"persistence effect count drift: got {counts}, expected {EXPECTED_COUNTS}")
    if any(a.effect_index >= b.effect_index for a, b in zip(entries, entries[1:])):
        fail("persistence effect entries are not strictly ordered by KTRF effect index")

    payload_sha = payload_sha256(entries)
    if payload_sha != EXPECTED_PAYLOAD_SHA256:
        fail(f"oracle payload hash drift: got {payload_sha}, expected {EXPECTED_PAYLOAD_SHA256}")

    global_entries = build_global_entries(document)
    global_payload_sha = global_payload_sha256(global_entries)
    if global_payload_sha != EXPECTED_GLOBAL_PAYLOAD_SHA256:
        fail(
            "global persistence payload hash drift: "
            f"got {global_payload_sha}, expected {EXPECTED_GLOBAL_PAYLOAD_SHA256}"
        )

    text = render(
        entries, global_entries, counts, routing_sha, route_source_sha, payload_sha
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8", newline="\n")
    print(
        f"wrote {args.output}: entries={counts['persistent_entries']} "
        f"type3={counts['type3']} type11={counts['type11']} "
        f"no_persistence={counts['no_persistence']} payload_sha256={payload_sha}"
    )


if __name__ == "__main__":
    main()
