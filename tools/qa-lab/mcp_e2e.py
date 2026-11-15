#!/usr/bin/env python3
"""Manual end-to-end test: invoke one real batch through the stdio MCP server."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import time

import lab_core as lab


def main() -> int:
    server = Path(__file__).parent / "lab_mcp.py"
    proc = subprocess.Popen([sys.executable, str(server)],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, encoding="utf-8", text=True)
    assert proc.stdin is not None and proc.stdout is not None

    def request(request_id: int, method: str, params: dict) -> dict:
        proc.stdin.write(json.dumps({"jsonrpc": "2.0", "id": request_id,
                                     "method": method, "params": params}) + "\n")
        proc.stdin.flush()
        line = proc.stdout.readline()
        if not line:
            raise RuntimeError("MCP server exited before sending a response")
        response = json.loads(line)
        if response.get("id") != request_id or "error" in response:
            raise RuntimeError("Invalid MCP response: " + line)
        return response["result"]

    try:
        handshake = request(1, "initialize", {
            "protocolVersion": "2025-11-25", "capabilities": {},
            "clientInfo": {"name": "kotonoha-qa-lab-e2e", "version": "1"}})
        assert handshake["serverInfo"]["name"] == "kotonoha-qa-lab"
        proc.stdin.write('{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
        proc.stdin.flush()
        tool_list = request(2, "tools/list", {})
        assert any(tool["name"] == "lab_start" for tool in tool_list["tools"])
        started = request(3, "tools/call", {"name": "lab_start", "arguments": {
            "mode": "smoke", "count": 1, "parallel": 1, "timeout": 30}})
        if started.get("isError"):
            raise RuntimeError(str(started["content"]))
        run_id = json.loads(started["content"][0]["text"])["run_id"]
        print("MCP_LAUNCHED_RUN=" + run_id, flush=True)
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            reply = request(4, "tools/call", {"name": "lab_status",
                                             "arguments": {"run_id": run_id}})
            state = json.loads(reply["content"][0]["text"])
            if state["status"] in ("passed", "failed", "error", "launch_failed"):
                print("MCP_RUN_STATUS=" + state["status"], flush=True)
                print("MCP_RUN_SUMMARY=" + json.dumps(state.get("summary", {})), flush=True)
                return 0 if state["status"] == "passed" else 1
            time.sleep(0.4)
        print("Run is still in progress; inspect manually: " + run_id, file=sys.stderr)
        return 2
    finally:
        proc.stdin.close()
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            print("MCP test process did not stop promptly", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
