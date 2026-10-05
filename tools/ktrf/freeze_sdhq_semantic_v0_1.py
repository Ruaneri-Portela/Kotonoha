#!/usr/bin/env python3
"""Generate the School Days HQ KTRF semantic-freeze manifest.

This tool is intentionally deterministic: it records hashes and conformance
facts, but no wall-clock timestamp.  It is run only after the validation and
differential gates have passed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any, Iterable, Mapping


ORACLE_COMMIT = "614461c2b14951ba117b9d2dedb4983cfa8ae8e6"
ORACLE_TAG = "school-days-routing-oracle-v1"
ORACLE_ROUTE_DATA_SHA256 = "031b53c172bb37e87a41356a8c17d84fd173606c7d0ea509607578a4629f921b"
SEMANTIC_VERSION = "0.1.0"
PROFILE_ID = "overflow.school-days-hq"
PROFILE_VERSION = "0.1.0"

EXPECTED_IR = {
    "nodes": 1857,
    "transitions": 2458,
    "choices": 287,
    "expressions": 1892,
    "effects": 11356,
    "variables": 497,
    "resource_locators": 1855,
    "external_hooks": 3,
    "endings": 22,
}

SEMANTIC_CONTRACT_FILES = [
    "docs/ktrf/SPECIFICATION.md",
    "docs/ktrf/DATA_MODEL.md",
    "docs/ktrf/ROUTING_IR.md",
    "docs/ktrf/VERSIONING_EXTENSIONS.md",
    "schemas/ktrf-routing-ir.schema.json",
    "tools/ktrf/interpreter.py",
    "tools/ktrf/validate_ir.py",
]

PROFILE_MAPPING_FILES = [
    "docs/ktrf/profiles/school-days-hq.md",
    "docs/ktrf/SCHOOL_DAYS_EXPORT.md",
    "docs/ktrf/ORACLE_BOUNDARY_NOTE.md",
    "tools/ktrf/export_sdhq_to_ir.py",
    "tools/ktrf/validate_sdhq_ir.py",
]

ORACLE_FILES = [
    "src/SchoolDaysRouteData.generated.inc",
    "src/SchoolDaysRouter.cpp",
    "include/Kotonoha/SchoolDaysRouter.hpp",
    "tests/SchoolDaysOracleTrace.cpp",
    "tests/SchoolDaysFullRouterTest.cpp",
    "docs/school-days-routing-validation/certified/ROUTING_MODEL_CATALOG.json",
    "docs/school-days-routing-validation/certified/ROUTING_MODEL_FREEZE.json",
]

CONFORMANCE_TOOL_FILES = [
    "tools/ktrf/audit_sdhq_differential_coverage.py",
    "tools/ktrf/diff_sdhq_witnesses.py",
    "tools/ktrf/diff_sdhq_targeted_transitions.py",
    "tools/ktrf/diff_sdhq_choices.py",
    "tools/ktrf/diff_sdhq_effect_stress.py",
    "tools/ktrf/diff_sdhq_state_matrix.py",
    "tools/ktrf/sdhq_injected_state_interpreter.py",
    "tests/ktrf/test_validate_ir.py",
    "tests/ktrf/test_export_sdhq_ir.py",
    "tests/ktrf/test_interpreter.py",
]

DEFAULT_REPORTS = {
    "witness_coverage": "build/ktrf/school-days-hq.differential-coverage.json",
    "targeted_transition": "build/ktrf/school-days-hq.targeted-transition-diff.json",
    "choice_feeling": "build/ktrf/school-days-hq.choice-diff.json",
    "source_effect_stress": "build/ktrf/school-days-hq.effect-stress-diff.json",
    "state_matrix": "build/ktrf/school-days-hq.state-matrix-diff.json",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> Mapping[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, Mapping):
        raise RuntimeError(f"JSON root is not an object: {path}")
    return value


def git(repo: Path, *args: str) -> str:
    completed = subprocess.run(
        ["git", *args],
        cwd=repo,
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"git {' '.join(args)} failed ({completed.returncode})\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return completed.stdout.strip()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def file_record(repo: Path, relative: str) -> dict[str, Any]:
    path = repo / relative
    require(path.is_file(), f"required freeze input is missing: {relative}")
    return {
        "path": relative.replace("\\", "/"),
        "bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def records_for(repo: Path, paths: Iterable[str]) -> list[dict[str, Any]]:
    return [file_record(repo, path) for path in paths]


def aggregate_digest(records: Iterable[Mapping[str, Any]]) -> str:
    digest = hashlib.sha256()
    for row in sorted(records, key=lambda r: str(r["path"])):
        digest.update(str(row["path"]).encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(row["sha256"]).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def ensure_relevant_tree_clean(repo: Path) -> None:
    tracked = sorted(
        set(SEMANTIC_CONTRACT_FILES)
        | set(PROFILE_MAPPING_FILES)
        | set(ORACLE_FILES)
        | set(CONFORMANCE_TOOL_FILES)
        | {
            "tools/ktrf/freeze_sdhq_semantic_v0_1.py",
            "tools/ktrf/run_sdhq_semantic_freeze.ps1",
            "docs/ktrf/SEMANTIC_FREEZE_GATE.md",
        }
    )
    status = git(repo, "status", "--porcelain=v1", "--", *tracked)
    require(
        not status,
        "KTRF semantic-freeze inputs have uncommitted changes:\n" + status,
    )


def validate_ir(ir: Mapping[str, Any]) -> dict[str, int]:
    require(ir.get("format") == "ktrf-routing-ir", "unexpected IR format")
    require(ir.get("ir_version") == SEMANTIC_VERSION, "unexpected IR version")
    profile = ir.get("profile")
    require(isinstance(profile, Mapping), "IR profile is missing")
    require(profile.get("id") == PROFILE_ID, f"unexpected profile id: {profile.get('id')!r}")
    require(profile.get("version") == PROFILE_VERSION, f"unexpected profile version: {profile.get('version')!r}")

    inventory: dict[str, int] = {}
    for key, expected in EXPECTED_IR.items():
        value = ir.get(key)
        require(isinstance(value, list), f"IR collection {key!r} is not an array")
        inventory[key] = len(value)
        require(inventory[key] == expected, f"IR {key} count mismatch: {inventory[key]} != {expected}")
    return inventory


def validate_reports(reports: Mapping[str, Mapping[str, Any]]) -> dict[str, Any]:
    coverage = reports["witness_coverage"]
    require(coverage.get("format") == "ktrf-sdhq-differential-coverage-v0.1", "unexpected witness coverage format")
    require(coverage.get("oracle_commit") == ORACLE_COMMIT, "witness coverage oracle mismatch")
    corpus = coverage.get("corpus", {})
    cov = coverage.get("coverage", {})
    require(isinstance(corpus, Mapping) and corpus.get("witnesses") == 22, "witness count mismatch")
    require(corpus.get("steps") == 4673, "witness step count mismatch")
    transitions = cov.get("transitions", {}) if isinstance(cov, Mapping) else {}
    require(isinstance(transitions, Mapping), "witness transition coverage missing")
    require(transitions.get("covered") == 1081 and transitions.get("total") == 2458, "witness transition coverage mismatch")

    targeted = reports["targeted_transition"]
    require(targeted.get("status") == "PASS", "targeted transition report is not PASS")
    require(targeted.get("oracle_commit") == ORACLE_COMMIT, "targeted transition oracle mismatch")
    targeted_diff = targeted.get("differential", {})
    combined = targeted.get("combined_coverage", {})
    require(isinstance(targeted_diff, Mapping) and targeted_diff.get("compared") == 1377 and targeted_diff.get("divergences") == 0, "targeted differential mismatch")
    require(isinstance(combined, Mapping) and combined.get("covered") == 2458 and combined.get("total") == 2458, "combined transition closure mismatch")

    choice = reports["choice_feeling"]
    require(choice.get("status") == "PASS", "Choice/Feeling report is not PASS")
    require(choice.get("oracle_commit") == ORACLE_COMMIT, "Choice/Feeling oracle mismatch")
    require(choice.get("choices") == 287 and choice.get("choice_outcomes") == 772, "Choice inventory mismatch")
    require(choice.get("scenarios") == 1544 and choice.get("divergences") == 0, "Choice scenario/differential mismatch")
    resolutions = choice.get("feeling_resolutions", {})
    deltas = choice.get("feeling_deltas", {})
    require(isinstance(resolutions, Mapping) and resolutions.get("covered") == 772 and resolutions.get("total") == 772, "FeelingResolution coverage mismatch")
    require(isinstance(deltas, Mapping) and deltas.get("covered") == 302 and deltas.get("total") == 302, "FeelingDelta coverage mismatch")

    effects = reports["source_effect_stress"]
    require(effects.get("status") == "PASS", "source Effect stress report is not PASS")
    require(effects.get("oracle_commit") == ORACLE_COMMIT, "source Effect oracle mismatch")
    effect_inventory = effects.get("source_effect_inventory", {})
    effect_diff = effects.get("differential", {})
    require(isinstance(effect_inventory, Mapping) and effect_inventory.get("covered") == 6183 and effect_inventory.get("total") == 6183, "source Effect coverage mismatch")
    require(effects.get("transitions_with_source_effects") == 2458, "effect-bearing transition count mismatch")
    require(effects.get("scenarios") == 4916, "effect stress scenario count mismatch")
    require(isinstance(effect_diff, Mapping) and effect_diff.get("compared") == 4916 and effect_diff.get("divergences") == 0, "effect stress differential mismatch")

    matrix = reports["state_matrix"]
    require(matrix.get("status") == "PASS", "broader state matrix report is not PASS")
    require(matrix.get("oracle_commit") == ORACLE_COMMIT, "state matrix oracle mismatch")
    matrix_cov = matrix.get("coverage", {})
    matrix_diff = matrix.get("differential", {})
    generation = matrix.get("generation", {})
    require(isinstance(matrix_cov, Mapping), "state matrix coverage is missing")
    source_nodes = matrix_cov.get("source_nodes", {})
    selected = matrix_cov.get("selected_transitions", {})
    pending = matrix_cov.get("choice_pending_default_checks", {})
    require(isinstance(source_nodes, Mapping) and source_nodes.get("covered") == 1857 and source_nodes.get("total") == 1857, "state matrix Node coverage mismatch")
    require(isinstance(selected, Mapping) and selected.get("covered") == 2458 and selected.get("total") == 2458, "state matrix Transition coverage mismatch")
    require(isinstance(pending, Mapping) and pending.get("covered") == 287 and pending.get("total") == 287, "pending Choice gate coverage mismatch")
    require(isinstance(generation, Mapping) and generation.get("max_pairwise_per_node") == 32, "state matrix pairwise policy mismatch")
    require(generation.get("unique_scenarios") == 3371, "state matrix unique scenario count mismatch")
    require(isinstance(matrix_diff, Mapping) and matrix_diff.get("compared") == 3371 and matrix_diff.get("divergences") == 0, "state matrix differential mismatch")

    return {
        "witnesses": 22,
        "witness_steps": 4673,
        "witness_transitions": 1081,
        "targeted_transitions": 1377,
        "combined_transition_coverage": "2458/2458",
        "choice_nodes": 287,
        "choice_outcomes": 772,
        "feeling_resolutions": 772,
        "feeling_deltas": 302,
        "choice_scenarios": 1544,
        "source_effects": 6183,
        "effect_stress_scenarios": 4916,
        "state_matrix_nodes": 1857,
        "state_matrix_transitions": 2458,
        "state_matrix_pending_choices": 287,
        "state_matrix_scenarios": 3371,
        "total_recorded_divergences": 0,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Freeze School Days HQ KTRF semantic conformance v0.1")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--ir", type=Path, default=Path("build/ktrf/school-days-hq.routing.json"))
    parser.add_argument("--output", type=Path, default=Path("build/ktrf/school-days-hq.semantic-freeze-v0.1.json"))
    parser.add_argument("--sha256-output", type=Path, default=Path("build/ktrf/school-days-hq.semantic-freeze-v0.1.sha256"))
    args = parser.parse_args()

    repo = args.repo.resolve()
    ensure_relevant_tree_clean(repo)

    head = git(repo, "rev-parse", "HEAD")
    branch = git(repo, "branch", "--show-current")
    ancestry = subprocess.run(
        ["git", "merge-base", "--is-ancestor", ORACLE_COMMIT, "HEAD"],
        cwd=repo,
        check=False,
    )
    require(ancestry.returncode == 0, f"HEAD is not descended from frozen oracle {ORACLE_COMMIT}")

    route_data = repo / "src/SchoolDaysRouteData.generated.inc"
    require(sha256_file(route_data) == ORACLE_ROUTE_DATA_SHA256, "frozen SchoolDaysRouteData.generated.inc SHA-256 mismatch")

    ir_path = args.ir if args.ir.is_absolute() else repo / args.ir
    ir_path = ir_path.resolve()
    require(ir_path.is_file(), f"canonical IR is missing: {ir_path}")
    ir = load_json(ir_path)
    inventory = validate_ir(ir)

    report_docs: dict[str, Mapping[str, Any]] = {}
    report_records: list[dict[str, Any]] = []
    for name, relative in DEFAULT_REPORTS.items():
        path = repo / relative
        require(path.is_file(), f"conformance report is missing: {relative}")
        report_docs[name] = load_json(path)
        record = file_record(repo, relative)
        record["name"] = name
        report_records.append(record)
    acceptance = validate_reports(report_docs)

    semantic_records = records_for(repo, SEMANTIC_CONTRACT_FILES)
    profile_records = records_for(repo, PROFILE_MAPPING_FILES)
    oracle_records = records_for(repo, ORACLE_FILES)
    conformance_records = records_for(repo, CONFORMANCE_TOOL_FILES)

    ir_relative = ir_path.relative_to(repo).as_posix()
    ir_record = file_record(repo, ir_relative)

    input_groups = {
        "semantic_contract": semantic_records,
        "school_days_profile_mapping": profile_records,
        "frozen_oracle": oracle_records,
        "conformance_toolchain": conformance_records,
    }
    group_digests = {
        name: aggregate_digest(rows)
        for name, rows in input_groups.items()
    }

    evidence_digest_rows = [ir_record, *report_records]
    evidence_digest = aggregate_digest(evidence_digest_rows)

    root_digest = hashlib.sha256()
    for name in sorted(group_digests):
        root_digest.update(name.encode("utf-8"))
        root_digest.update(b"\0")
        root_digest.update(group_digests[name].encode("ascii"))
        root_digest.update(b"\n")
    root_digest.update(b"evidence\0")
    root_digest.update(evidence_digest.encode("ascii"))
    root_digest.update(b"\n")

    manifest = {
        "format": "ktrf-sdhq-semantic-freeze-v0.1",
        "semantic_version": SEMANTIC_VERSION,
        "profile": {"id": PROFILE_ID, "version": PROFILE_VERSION},
        "oracle": {
            "tag": ORACLE_TAG,
            "commit": ORACLE_COMMIT,
            "route_data_sha256": ORACLE_ROUTE_DATA_SHA256,
        },
        "repository": {
            "branch": branch,
            "head_commit": head,
            "note": "head_commit identifies the implementation used to produce this manifest; unrelated working-tree changes are excluded by scoped cleanliness checks",
        },
        "canonical_ir": {
            **ir_record,
            "inventory": inventory,
        },
        "input_groups": input_groups,
        "input_group_sha256": group_digests,
        "conformance_reports": report_records,
        "evidence_sha256": evidence_digest,
        "acceptance": acceptance,
        "semantic_freeze_root_sha256": root_digest.hexdigest(),
        "status": "PASS",
    }

    output = args.output if args.output.is_absolute() else repo / args.output
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")

    manifest_hash = sha256_file(output)
    hash_output = args.sha256_output if args.sha256_output.is_absolute() else repo / args.sha256_output
    hash_output = hash_output.resolve()
    hash_output.parent.mkdir(parents=True, exist_ok=True)
    hash_output.write_text(f"{manifest_hash}  {output.name}\n", encoding="ascii", newline="\n")

    print("=== KTRF / School Days semantic freeze v0.1 ===")
    print(f"oracle={ORACLE_COMMIT}")
    print(f"head={head}")
    print(f"ir_sha256={ir_record['sha256']}")
    print(f"semantic_contract_sha256={group_digests['semantic_contract']}")
    print(f"profile_mapping_sha256={group_digests['school_days_profile_mapping']}")
    print(f"oracle_inputs_sha256={group_digests['frozen_oracle']}")
    print(f"conformance_toolchain_sha256={group_digests['conformance_toolchain']}")
    print(f"evidence_sha256={evidence_digest}")
    print(f"semantic_freeze_root_sha256={manifest['semantic_freeze_root_sha256']}")
    print(f"manifest_sha256={manifest_hash}")
    print(f"output={output}")
    print(f"sha256_output={hash_output}")
    print("KTRF SEMANTIC FREEZE MANIFEST PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
