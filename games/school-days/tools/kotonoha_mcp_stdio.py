#!/usr/bin/env python3
"""VS Code/Codex MCP entrypoint for the native Kotonoha engine (Windows L4).

Each MCP session starts the newly compiled *Kotonoha* SchoolDays.exe with a
synthetic fixture, software renderer and isolated --documents/--config/root.
The executable already implements the MCP JSON-RPC stdio protocol; this thin
wrapper provides safe paths and a dedicated working directory, not a second
MCP server. No port, retail EXE, commercial GPK or real saves are used.

All output on stdout belongs to MCP, so errors must go to stderr.
"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


REPO = Path(__file__).resolve().parents[3]
BUILD = REPO / "build" / "school-days-l4-two-dll-staticmd"
GAME = BUILD / "games" / "school-days" / "Release" / "SchoolDays.exe"
FIXTURE = BUILD / "games" / "school-days" / "tests" / "fixtures"
ROUTE = REPO / "games" / "school-days" / "game-assets" / "school-days-hq.ktnroute"


def main():
    if os.name != "nt":
        print("This Kotonoha Windows L4 MCP profile requires Windows.", file=sys.stderr)
        return 2
    if not GAME.is_file() or not (FIXTURE / "Script").is_dir() or not ROUTE.is_file():
        print("Build the Windows L4 SchoolDays.exe and its CTest fixtures before starting MCP.",
              file=sys.stderr)
        return 2
    scratch = BUILD / "qa-mcp-vscode"
    scratch.mkdir(parents=True, exist_ok=True)
    # Never open or mutate real saves and never let file tools escape the
    # session directory. Every restart has an independent documents folder.
    session = Path(tempfile.mkdtemp(prefix="session-", dir=scratch))
    settings = session / "settings.json"
    env = os.environ.copy()
    env.update({"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"})
    env["PATH"] = str(GAME.parent) + os.pathsep + env.get("PATH", "")
    argv = [str(GAME), "--school-days", str(FIXTURE), "--ktrf", str(ROUTE),
            "--documents", str(session), "--config", str(settings),
            "--no-config", "--no-launcher", "--no-asset-overlay",
            "--renderer", "software", "--node", "0", "--mcp",
            "--mcp-root", str(session)]
    # Pass stdio handles straight through (JSON-RPC requests/responses).
    # There is no custom TCP bridge and no extra MCP protocol dispatcher.
    try:
        return subprocess.call(argv, cwd=GAME.parent, env=env)
    except OSError as error:
        print(f"Cannot start native Kotonoha MCP: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
