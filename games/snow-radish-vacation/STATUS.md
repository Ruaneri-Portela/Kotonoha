# Snow Radish Vacation!! — Port Status

## Engine
Overflow ADV 3.05 (`of_adv3.exe`, MSVC 6.0, `800×600`)

## Status: Playable ✅

### REA Audit (2026-11-18)
- Full 1:1 clean-room reverse engineering of `of_adv3.exe` (499,712 bytes)
- 46 sys natives audited across 33 `.obj` scripts
- All critical route blockers fixed

### Fixes Applied
| Fix | Description |
|-----|-------------|
| **A1** | `op=0x10` indirect store (`mode=0x42`) — 6 instructions in `lib.obj` were silently dropped |
| **A2** | `sys 7` route choice — separated from `sys 1` dialogue; now handles 1-based text choice menu (18 choice points, 10 route scripts) |
| **A3** | `sys 17` var reset — clears banks 0–4 (`0x0000..0x4fff`), preserving bank 5 system vars |
| **A4** | `sys 26` quit — pushes `SDL_EVENT_QUIT` |
| **A5** | `sys 32` click-wait — blocks without message window (uniform preview + ending CG pause) |
| **A6** | `sys 33` timed wait — skippable `ms` delay with `TimerSkippable` |
| **A7** | `sys 52` CG pan — oversized CG vertical panning (3 calls in `lib.obj`) |
| **A8** | `sys 209/225/228` gallery — CG gallery + scene replay system stubs |
| **A9** | `sys 223` staff roll — ending Credit00..14 + snow particles callback |

### Additional Natives
- `sys 3/27`: Hide/reset message window
- `sys 18/25`: Chapter/scene title for save labels
- `sys 21`: Debug assertion log
- `sys 136`: Voice stop (routes to `sys 132`)

### Build & Test
- All CTests pass
- Smoke boot exits with code 0 against retail assets
