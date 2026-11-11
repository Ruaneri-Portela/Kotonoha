# School Days HQ local parity control

This Windows DEV tool controls the original and Kotonoha through separate
stdio MCP servers. Merely starting an MCP server does **not** start a game.
Only `game_start`, `original_start`, or `start_both` launches processes.
No original EXE, DLL, or GPK is patched.

## Local installation and MCP configuration

Copy [mcp-servers.example.json](mcp-servers.example.json) to a local,
untracked MCP client configuration and replace its example `cwd` paths with
your checkout's `tools` directory. Configure its Python command to use your
local Python 3.10 environment; do not publish machine-specific configuration.
Its three entries use the already-created Python 3.10 environment at
`D:\Dev\Kotonoha-v3\build\school-days-parity-venv\Scripts\python.exe`.
The command is that absolute Python path; arguments are
`-m schooldays_parity.server --mode kotonoha|original|parity`, with
`--port 47321` for the two bridge clients. The working directory is
`D:\Dev\Kotonoha-v3\tools`. The only configured environment values are
`PYTHONUTF8=1`, `PYTHONPATH`, and, where applicable,
`KOTONOHA_DEV_PORT=47321`.
No secret or remote address is configured.
`PYTHONPATH` points to the same `tools` directory, so clients that ignore
the `cwd` field can still import the package.

To recreate this environment, run Python 3.10 venv and install
`tools\schooldays_parity\requirements.txt`. The WGC dependency is pinned
to `windows-cap==0.1.2` because its published binary supports Python 3.10;
GDI remains available if WGC cannot initialize or produce a fresh frame.

In the original local QA setup, the Kotonoha DEV binary was
`D:\Dev\Kotonoha-v3\build\school-days-dev-control4\games\school-days\Release\SchoolDays.exe`.

`D:\SCHOOL DAYS HQ\Overflow\SCHOOLDAYS HQ\SCHOOLDAYS HQ.exe`.
The Kotonoha controller passes `--school-days` with the original directory
so both processes read the same retail Packs. Kotonoha writes its own
profile beside the isolated DEV EXE.
For a direct double-click of the DEV EXE, run
`tools\schooldays_parity\prepare-local-dev-build.ps1` after building. It
creates an idempotent `Packs` junction beside the DEV executable, pointing
to the local retail 29-pack set. Without that link or an explicit
`--school-days` argument, the DEV build has only its route/UI assets and
cannot find the game data. The script does not copy or alter the GPK files.
Only one DEV process can own port 47321 at a time. Close a previously opened
DEV build before launching this one on the default port.

The DEV bridge binds only `127.0.0.1:47321`. Its newline JSON protocol
requires `hello` with `{"protocolVersion":1}`; the Python controller
rejects any other version before a status or input call.
The bridge accepts at most four clients and 64 KiB per request. It has no
shell or arbitrary filesystem method.

## Health and tools

Call `health` on each MCP server before starting games. It returns server
version, executable presence, process/window/transport status, capture
backend, and bridge protocol compatibility. Parity health includes both.
For a paired session, use parity `start_both` as the sole process owner.
The three MCP server processes do not share Python controller state; starting
Kotonoha separately and then calling parity `start_both` would attempt to
launch another DEV process on the same local port and is rejected by PID
verification.

Kotonoha tools include `game_start`, `game_stop`, `game_status`, scene,
script, node, timeline, choices, media, audio, ABC, menu, UI, display and
transition state, `click`, mouse and key events, `pause_runtime`,
`resume_runtime`, `step_frame`, `wait_frames`, `capture_frame`, and logs.
`get_logs` returns both the transition/event trace and `engineLogs`, including
the exact missing-asset path and prefetch operation. Frame hashes and
luminance are read on request, so an idle bridge does not stall the renderer
with full-frame GPU readbacks.
For a read-only inventory of retail ORS references against all 29 GPK
indexes, run `python tools\schooldays_parity\audit_retail_assets.py
"D:\SCHOOL DAYS HQ\Overflow\SCHOOLDAYS HQ\Packs"` from the repository root.
It does not start either game.
The original tools include `original_start`, `original_stop`, status,
window, capture, mouse/key input and action logs. Parity tools include
`start_both`, `stop_both`, `capture_both`,
`click_both_normalized`, `key_both`, `record_transition`,
`compare_last_transition`, `analyze_transition`,
`compare_choice_transition`, `compare_audio_metrics`,
`run_input_sequence`, and `get_parity_report`.

## Comparing a choice

1. Call parity `start_both`, then `health`.
2. Advance both games to the same visible choice using normalized input.
3. Call `capture_both` to check alignment and the reported
   `captureDeltaMs`.
4. Call `compare_choice_transition` with the **manual normalized** center
   of that option, for example
   `{"x":0.73,"y":0.61,"durationMs":1000,"fpsTarget":30,"label":"choice-1"}`.
5. Inspect its transition directory and `get_parity_report`.

The helper verifies a Kotonoha choice through `get_choices`. It does not
claim to know the original's choice text or index. It captures a preframe,
starts the transition recording, sends the mirrored click with an action ID,
and captures immediately afterward. `analyze_transition` reports the
original's first significant visual change, black frames and first stable
postframe. Original choice matching is the caller's responsibility.

For manual operation: `start_both` → `health` → `capture_both` →
`click_both_normalized` → `record_transition` →
`compare_last_transition` → `get_parity_report`.
The action ID is included in input markers and captured frame metadata.
Kotonoha's marker records the engine frame that received the synthetic
event; it does not prove that a particular game UI widget accepted it.

## Frames, timing and black-flash detection

The original QA setup's output root was
`D:\Dev\Kotonoha-v3\build\school-days-parity-captures`.
`record_transition` creates `<UTC timestamp>-<sanitized label>` containing
`original/`, `kotonoha/`, `heatmaps/`, `metadata.json` and, after
comparison, `comparison.json`. Each final frame is a PNG with a JSON
sidecar. Clients cannot choose output paths.

Every frame reports a monotonic timestamp, UTC save timestamp, dimensions,
RGBA SHA256, mean luminance, near-black ratio and backend. Kotonoha also
reports engine frame/update, transition generation, scene and node.
The two captures are requested concurrently. Their monotonic timestamp
difference is `captureDeltaMs`. The Kotonoha socket round-trip contributes
`captureUncertaintyMs`; their sum is `syncUpperBoundMs`. If that upper bound
exceeds `maxSyncMs` (50 by default), pixel comparison is marked
`UNSYNCHRONIZED`.

The window detector checks a configurable `blackLuma` (default 12),
`blackRatio` (0.95), `normalRatio` (0.7), and `normalWindow` (4 paired
frames). It reports `BLACK_FLASH_CANDIDATE` for a valid → near-black →
valid Kotonoha sequence absent in the original when timing is weak;
`BLACK_FLASH_DIVERGENCE` requires acceptable synchronization and sampling
gaps throughout that window. `maxGapMs` defaults to twice the requested
frame period and can be supplied to `compare_last_transition`. The record
reports achieved FPS and its largest capture gap. Each frame also has
luminance deltas to its neighbors.
The detector is evidence for review, not an automatic parity verdict.

## Capture and audio limits

The original controller tries Windows Graphics Capture through
`windows-cap`; it uses GDI `BitBlt` on the foreground, visible client
area when WGC is unavailable or stalls. `captureBackend` and fallback
reason are exposed. WGC operation with this game's renderer still requires
runtime validation. No DLL injection is used. The original has no
authenticated internal scene or frame counter, so even synchronized
timestamps do not prove identical semantic frame timing.

Kotonoha DEV measures peak, RMS, near-saturated sample count and silent
postmix block candidates for each SDL logical audio device/channel. Its
reported master values aggregate these observations; Windows combines the
separate logical devices afterward, so this is **not** the physical final
mix. A silent block can be intentional silence, so the underrun number is
explicitly a candidate count. Original per-process audio capture is not
enabled: Microsoft documents `AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK`
with `ActivateAudioInterfaceAsync`, but it needs a dedicated native COM
capture helper and OS capability validation. System-wide loopback would not
be a truthful substitute.
This host reports Windows build 19045; the documented per-process API
requires build 20348 or later. No original audio metrics are emitted here.
Microsoft's process-loopback API is documented at
https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ne-audioclientactivationparams-audioclient_activation_type.
