# School Days HQ — offline parity analysis helpers

This directory retains **offline, read-only analysis tools** for investigating
the original retail resources. It no longer provides a School Days-specific
MCP server, custom TCP protocol or process-control bridge.

## MCP belongs to the Kotonoha engine

The only supported MCP implementation is `lib/src/mcp/MCPServer.cpp`.
Enable it explicitly with the engine's `--mcp` (stdio) or its opt-in loopback
socket option; see the engine command-line help. Do **not** run or recreate
`schooldays_parity.server`, `SchoolDaysDevBridge`, or a listener on the old
port 47321. They have been removed; any old local client configurations must
be discarded.

The engine owns MCP tool dispatch, filesystem restrictions and transport.
School Days is just another Kotonoha game and does not create a second server.

## Remaining offline utilities

- `audit_retail_assets.py`: offline check of ORS asset references against a
  provided local GPK directory. This does not run the original executable.
- `compare.py`: Pillow-based still-image comparison and frame statistics.
- `trace_original_session.py` (local/private, if present): old observer script
  for research; **not** part of the engine, not a supported MCP controller.

The former Python `server.py`, dedicated socket `controllers.py`, paired-game
`parity.py` and `mcp-servers*.json` were intentionally removed. The generic
engine socket speaks the same JSON-RPC/MCP protocol as its stdio transport,
**not** the old game-specific `hello`/`click`/`get_scene` protocol.

Commercial GPK files, proprietary DLLs and user saves must be supplied only
by their owners for lawful local testing. No source files or build scripts
should modify the original retail installation.
