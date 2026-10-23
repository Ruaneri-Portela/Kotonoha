# ORS-only Natural Execution Closure — 2026-10-04

## Final result

The installed School Days HQ edition contains two physical ORS scripts that are absent from the recovered 1,857-node natural router:

- `01/01-00-OP2`
- `05/05-9O-B00`

After closing every known natural SceneKey producer domain, both are classified:

| Target | Natural execution | Externally forced load | Router node |
|---|---|---|---|
| `01/01-00-OP2` | **NO** | **POSSIBLE** | **NO** |
| `05/05-9O-B00` | **NO** | **POSSIBLE** | **NO** |

`NATURAL_EXECUTION = NO` means no audited producer in the installed game generates the target name. It does **not** mean the generic loader is physically incapable of opening the file if an external/manual state supplies that name.

## Closed producer domains

The following natural name sources are closed:

1. installed `STARTSCRIPT.INI` startup values;
2. RouteProc `Next` / `Back` outputs;
3. `[Next]` -> `ScriptObject+0x114`;
4. SysMenu/replay -> `main+0x154` (166 audited pointers, 122 distinct SceneKeys);
5. installed SLog type-1 save launch names;
6. SLog type-3 record `+0x0C` names, naturally produced from the 55 RouteProc tables or restored from installed saves;
7. restore replacement of ScriptObject, whose new `+0x114` value is initialized empty;
8. `[Exit]` -> empty `+0x114`;
9. previously audited menu/direct-launch producer domains.

SLog type 4 does not feed the type-3 SceneKey launch field and does not feed `ScriptObject+0x114`.

## Inventory interpretation

The exact set differences remain:

```text
Router SceneKeys:       1857
Physical ORS SceneKeys: 1857

Router-only:
  03/03-B2-A00
  03/03-KB-E00

ORS-only:
  01/01-00-OP2
  05/05-9O-B00
```

Interpretation:

- the two **router-only** nodes are logical RouteProc dispatchers and require immediate resolution without Gameplay;
- the two **ORS-only** files are retained physical scripts that no natural installed-edition producer selects.

Therefore no ORS-only node should be added to the generated natural router.

## Engine consequence

The routing-only integration rule remains intentionally narrow:

- auto-resolve only the two proven no-ORS RouteProc dispatcher nodes;
- keep ordinary missing physical destinations as hard failures;
- do not special-case the two ORS-only physical scripts in the router.

## Next validation

Rebuild the Android validation branch and replay the incident that previously stopped at:

```text
next t917 -> 03/03-KB-E00
destination not loaded: 03/03-KB-E00
```

Expected corrected behavior:

```text
next t917 -> 03/03-KB-E00
routing-only node 03/03-KB-E00 (no ORS); resolving immediately
next t907 -> 03/03-KB-G00
```

or `t908 -> 03/03-KB-F00` if `001 > 002`.

After Route 15 passes, smoke-test `03/03-B2-A00` for the same virtual-node handling.
