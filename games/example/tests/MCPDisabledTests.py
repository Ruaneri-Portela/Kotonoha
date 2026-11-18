#!/usr/bin/env python3
"""Verify MCP is truly absent from a feature-disabled Kotonoha executable."""
import subprocess
import sys

program = sys.argv[1]
help_result = subprocess.run(
    [program, "--help"], capture_output=True, text=True, timeout=30
)
assert help_result.returncode == 0, (help_result.stdout, help_result.stderr)
output = help_result.stdout + help_result.stderr
assert "--mcp" not in output, "MCP CLI options are visible in an OFF build"

request = subprocess.run(
    [program, "--mcp"], capture_output=True, text=True, timeout=30
)
assert request.returncode != 0, "Disabled MCP CLI was accepted"
print("MCP is unavailable and hidden from the command-line help")
