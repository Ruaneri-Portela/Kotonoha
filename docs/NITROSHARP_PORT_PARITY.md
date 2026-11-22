# NitroSharp compatibility work in Kotonoha/N2System

This status file is intentionally conservative: a passing synthetic test is
**not** equivalent to running every commercial NSB scenario correctly.

Reference: [CommitteeOfZero/nitrosharp](https://github.com/CommitteeOfZero/nitrosharp),
branch `rebuild`. Additional opcode evidence:
[FGRE/libnpa](https://github.com/FGRE/libnpa), `include/nsbmagic.hpp`.

## Status by subsystem

| Area | Implemented in Kotonoha | Still needed for full 1:1 |
|---|---|---|
| Process/thread scheduling | Stable deque storage, cooperative budgets, frame-delayed process start, deferred process destruction, native Request actions, deferred save I/O | Separate process-local execution environments, exact NitroSharp World/Thread/Process lifecycle, cross-process alias scopes and timing |
| Builtin function dispatch | NSB Time, DateTime, ExistSave, media remaining/duration/elapsed, Save/Load/DeleteSaveFile, existing animation and dialogue/audio opcodes | Complete opcode coverage, exact operand signatures for every game version, full error/result semantics, platform-specific features |
| Animations | All eleven NitroSharp easing curves, cubic Bezier move, easing-aware move/fade/rotate/zoom/transition, VM animation waits | Composite 3-D transforms, camera effects, exact curve parameter conventions, masked transitions, easing across all runtime-created entities |
| Graphics | Cached RGBA monochrome and 8/16-pass premultiplied blur, `SetTone` (0x02e), `SetShade` (0x02d), color/alpha state restoration | GPU shader parity with NitroSharp, gradients/lens/outline/3D cube, full mask/effect graph and image comparisons on target GPUs |
| Text | Kotonoha::UI renderer text atlas with native game font bytes, no per-frame text-surface creation | Full NitroSharp markup parser, font weights, kerning, Japanese line breaks, ruby text, every engine-specific style/outline option |
| Persistence | N2SV binary extension (N2EX/N2RS) stores NSB calls, variables, processes, scene/animation, phone, audio routing, dialogue/backlog; strict JSON debug save roundtrip; corruption checks; deferred file I/O | Audio/video exact timestamp resumes, complete compatibility with original NitroSharp slot format, cross-version schema migrations and long-play retail validation |

## Regression coverage

- `Kotonoha.N2System.NsbVm` (CMake / CTest) validates NSB bytecode,
  native builtin callbacks, thread scheduling, nested process creation, deferred
  save/load/delete, extension and JSON roundtrip, truncated save rejection,
  graphics state persistence and frame resumption.
- `Kotonoha.N2System.EntityQuery` checks the source-derived query/path
  grammar, aliases, parents, deletion and insertion ordering.
- `Kotonoha.N2System.AnimationTiming` verifies all easing curves, bezier
  geometry, tween completion.
- `Kotonoha.N2System.ImageEffects` checks premultiplied alpha and effect
  selection.
- `.github/workflows/port-contracts.yml` compiles these sources in C++17;
  `.github/workflows/linux-system-deps.yml` syntax-compiles the N2System
  presentation runtime against SDL3.

## Validation required before claiming full parity

1. Provide **legally obtained** game data locally via
   `KOTONOHA_STEINSGATE_DIR`; never upload retail files to GitHub.
2. Run N2System from boot through title, choices, every major route and ending,
   including scripts with concurrently running processes.
3. Compare reference frames for overlays, fonts, video transitions, masks,
   text layout and shader output across Linux/Windows/macOS and mobile/web.
4. Verify save/reload while dialogue is revealing, during animations, during
   audio/video playback and with multiple background threads.
5. Confirm compatibility with existing saves and exercise parser corruption,
   low-memory, missing assets and failed storage operations.

Keep the integration PR **open** until the outstanding equivalence items and
retail tests are actually completed.
