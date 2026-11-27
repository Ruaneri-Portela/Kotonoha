#!/usr/bin/env python3
"""Minimal stdio MCP adapter for isolated Kotonoha QA batch jobs.

No network ports, external credentials or arbitrary shell/file tools. VS Code
launches this process with .vscode/mcp.json; ChatGPT Chat On Steroids can run
the exact same lab_cli.py commands in the shared workspace.
"""

from __future__ import annotations

import json
import sys

import lab_core as lab


TOOLS = [
    {"name": "lab_preflight", "description": "Read-only verification of the L4 synthetic test inputs and isolated output directory.",
     "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False}},
    {"name": "lab_start", "description": "Start an asynchronous, bounded batch of synthetic School Days renderer/settings smoke cases. Writes only under tools/qa-lab/state; GPU opt-in requires parallel=1. Returns a run ID immediately.",
     "inputSchema": {"type": "object", "properties": {
         "mode": {"type": "string", "enum": ["smoke", "matrix", "gpu"], "default": "smoke"},
         "count": {"type": "integer", "minimum": 1, "maximum": 256, "default": 4},
         "seed": {"type": "integer", "minimum": 0, "maximum": 4294967295, "default": 20261109},
         "parallel": {"type": "integer", "minimum": 1, "maximum": 4, "default": 2},
         "timeout": {"type": "integer", "minimum": 5, "maximum": 120, "default": 30}},
         "additionalProperties": False}},
    {"name": "lab_status", "description": "Get a run status and compact failures without sending complete process logs.",
     "inputSchema": {"type": "object", "properties": {"run_id": {"type": "string"}},
                     "required": ["run_id"], "additionalProperties": False}},
    {"name": "lab_runs", "description": "List recent runs and failure summaries from the isolated lab.",
     "inputSchema": {"type": "object", "properties": {"limit": {"type": "integer", "minimum": 1, "maximum": 40, "default": 10}},
                     "additionalProperties": False}},
    {"name": "lab_failures", "description": "Read the compact failures-only section of one finished run; never read arbitrary file paths.",
     "inputSchema": {"type": "object", "properties": {"run_id": {"type": "string"}},
                     "required": ["run_id"], "additionalProperties": False}},
]


def result(value: object, *, is_error: bool = False) -> dict:
    return {"content": [{"type": "text", "text": json.dumps(value, ensure_ascii=False)}],
            "isError": is_error}


def call(name: str, args: dict) -> object:
    expected = {
        "lab_preflight": set(), "lab_start": {"mode", "count", "seed", "parallel", "timeout"},
        "lab_status": {"run_id"}, "lab_runs": {"limit"}, "lab_failures": {"run_id"}}
    if name not in expected:
        raise lab.LabError("Unknown tool")
    if not isinstance(args, dict) or (set(args) - expected[name]):
        raise lab.LabError("Invalid tool arguments")
    if name == "lab_preflight":
        return {"status": "ready", "inputs": lab.source_ok(),
                "outputs": str(lab.STATE), "protocol": "MCP stdio"}
    if name == "lab_start":
        run_id = lab.start_job(args.get("mode", "smoke"), args.get("count", 4),
                               args.get("seed", 20261109), args.get("parallel", 2),
                               args.get("timeout", 30))
        return {"run_id": run_id, "status": "queued", "next_tool": "lab_status"}
    if name == "lab_status":
        return lab.status(args["run_id"])
    if name == "lab_runs":
        return lab.list_runs(args.get("limit", 10))
    if name == "lab_failures":
        run = lab.run_folder(args["run_id"])
        report = lab.read_json(run / "report.json")
        return {"run_id": args["run_id"], "failures": report["failures_only"],
                "passed": report["passed"], "failed": report["failed"]}
    raise lab.LabError("Unknown tool")


def handle(message: dict) -> dict | None:
    method = message.get("method")
    request_id = message.get("id")
    if method is None:
        return None
    if method.startswith("notifications/"):
        return None
    if request_id is None:
        return None
    try:
        if method == "initialize":
            params = message.get("params") or {}
            requested = params.get("protocolVersion", "2025-03-26")
            answer = {"protocolVersion": requested,
                      "capabilities": {"tools": {"listChanged": False}},
                      "serverInfo": {"name": "kotonoha-qa-lab", "version": "0.1.0"},
                      "instructions": "All runs use synthetic fixtures and write only to tools/qa-lab/state. Use lab_start, then lab_status or lab_failures."}
        elif method == "ping":
            answer = {}
        elif method == "tools/list":
            answer = {"tools": TOOLS}
        elif method == "tools/call":
            params = message.get("params") or {}
            try:
                answer = result(call(params.get("name", ""), params.get("arguments") or {}))
            except (lab.LabError, ValueError, TypeError, KeyError, OSError) as exc:
                answer = result({"error": str(exc)}, is_error=True)
        else:
            return {"jsonrpc": "2.0", "id": request_id,
                    "error": {"code": -32601, "message": "Method not found"}}
        return {"jsonrpc": "2.0", "id": request_id, "result": answer}
    except Exception as exc:
        return {"jsonrpc": "2.0", "id": request_id,
                "error": {"code": -32603, "message": str(exc)}}


def main() -> int:
    # MCP stdio messages are one UTF-8 JSON object per line. Never print
    # diagnostics to stdout or the client protocol stream is corrupted.
    for line in sys.stdin.buffer:
        try:
            message = json.loads(line)
            if not isinstance(message, dict):
                continue
            response = handle(message)
            if response is not None:
                sys.stdout.write(json.dumps(response, ensure_ascii=False) + "\n")
                sys.stdout.flush()
        except Exception as exc:
            print(f"qa-lab MCP: {exc}", file=sys.stderr, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
