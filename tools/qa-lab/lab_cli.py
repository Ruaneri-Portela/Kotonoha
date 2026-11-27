#!/usr/bin/env python3
"""Single entry point for ChatGPT/Chat On Steroids and VS Code QA tasks."""

from __future__ import annotations

import argparse
import json
import sys
import time

import lab_core as lab


def print_json(data: object) -> None:
    print(json.dumps(data, ensure_ascii=False, indent=2))


def format_case_progress(result: dict, completed: int, total: int) -> str:
    """Compact, human-readable progress for VS Code's integrated terminal."""
    params = result["parameters"]
    success = bool(result["pass"])
    normalizations = len(result.get("expected_normalizations", []))
    status = "PASS" if success else "FAIL"
    detail = (" | normalized=" + str(normalizations)) if normalizations else ""
    if not success:
        detail += " | " + ", ".join(str(msg) for msg in result.get("failures", [])[:2])
    return (f"[{completed:>3}/{total}] case-{result['id']:04d} "
            f"{params['renderer']} backend={params['backend']} "
            f"factor={params['factor']} preset={params['preset']} "
            f"scale={params['renderScale']} : {status}{detail}")


def watch_progress(run_id: str, poll_seconds: float = 0.2) -> int:
    """Stream each completed case, not just the final JSON, until job exits."""
    run = lab.run_folder(run_id)
    seen: set[int] = set()
    last_state = ""
    while True:
        job = lab.status(run_id)
        current_state = job["status"]
        if current_state != last_state:
            print(f"LAB_STATUS={current_state}", flush=True)
            last_state = current_state
        results = []
        for path in sorted(run.glob("case-*/result.json")):
            try:
                item = lab.read_json(path)
            except (OSError, ValueError):
                # File is written atomically; on transient errors retry later.
                continue
            if isinstance(item.get("id"), int) and item["id"] not in seen:
                results.append(item)
        for item in sorted(results, key=lambda entry: entry["id"]):
            seen.add(item["id"])
            print(format_case_progress(item, len(seen), job["count"]), flush=True)
        if current_state in ("passed", "failed", "error", "launch_failed"):
            summary = job.get("summary", {})
            print(f"LAB_FINISHED={current_state.upper()} "
                  f"passed={summary.get('passed', job.get('passed', 0))} "
                  f"failed={summary.get('failed', job.get('failed', 0))} "
                  f"expected_normalizations={summary.get('expected_normalizations', 0)}",
                  flush=True)
            print("LAB_REPORT=" + str(run / "report.json"), flush=True)
            if "error" in job:
                print("LAB_ERROR=" + str(job["error"]), file=sys.stderr, flush=True)
            return 0 if current_state == "passed" else 1
        time.sleep(poll_seconds)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="command", required=True)
    subs.add_parser("check", help="Read-only synthetic build preflight")
    launch = subs.add_parser("run", help="Launch isolated batch; returns run ID")
    launch.add_argument("--mode", choices=lab.available_modes(), default="smoke")
    launch.add_argument("--count", type=int, default=4)
    launch.add_argument("--seed", type=int, default=20261109)
    launch.add_argument("--parallel", type=int, default=2)
    launch.add_argument("--timeout", type=int, default=30)
    launch.add_argument("--wait", action="store_true", help="Wait for completion")
    internal = subs.add_parser("execute", help="Internal: execute a queued run")
    internal.add_argument("--run-id", required=True)
    check = subs.add_parser("status", help="Status and compact failure details")
    check.add_argument("--run-id", required=True)
    recent = subs.add_parser("list", help="Show recent run summaries")
    recent.add_argument("--limit", type=int, default=10)
    report = subs.add_parser("report", help="Print a complete saved report")
    report.add_argument("--run-id", required=True)
    args = parser.parse_args()

    try:
        if args.command == "check":
            print_json({"state_dir": str(lab.STATE), "inputs": lab.source_ok(),
                        "modes": lab.available_modes(),
                        "notes": "Only synthetic fixture; source build is read-only."})
        elif args.command == "execute":
            return lab.execute_job(args.run_id)
        elif args.command == "run":
            run_id = lab.start_job(args.mode, args.count, args.seed,
                                   args.parallel, args.timeout)
            print(f"LAB_RUN_ID={run_id}", flush=True)
            print(f"LAB_RUN_DIR={lab.run_folder(run_id)}", flush=True)
            if args.wait:
                return watch_progress(run_id)
        elif args.command == "status":
            print_json(lab.status(args.run_id))
        elif args.command == "list":
            print_json(lab.list_runs(args.limit))
        elif args.command == "report":
            path = lab.run_folder(args.run_id) / "report.json"
            print_json(lab.read_json(path))
        return 0
    except (lab.LabError, OSError, ValueError, KeyError) as exc:
        print(f"LAB_ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
