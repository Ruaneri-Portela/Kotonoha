# Save/SLog Direct-Launch Audit — 2026-10-04

## Result

The installed save format was reconstructed sufficiently to identify the script name that reaches the direct launcher.

```text
SaveFile00%d.DAT
  -> SLog type-1 first name
  -> 0x00433BA9
  -> 0x0042A760
  -> 0x0042A7AC
  -> 0x00430D20
  -> direct ORS loader
```

## Current-save findings

- 22 slot saves parsed to the final byte under the observed SLog grammar.
- First launch names are direct XOR-transformed UTF-16LE strings.
- Neither `01/01-00-OP2` nor `05/05-9O-B00` occurs in the confirmed launch field.
- 1,366 additional type-3/type-4 names were decoded; neither target occurs there.
- `GlobalFlag.DAT` was fully parsed; neither target occurs in decoded keys/string values.

Therefore:

- OP2 via existing saves: **NO-IN-EXISTING-SAVES**
- B00 via existing saves: **NO-IN-EXISTING-SAVES**
- Global reachability: **UNKNOWN**

## Remaining static gaps

- enumerate all writers/elements feeding `object+0x154`;
- enumerate all writers and restore effects for `ScriptObject+0x114`;
- determine whether any of those domains can produce OP2 or B00.

Runtime tracing is only needed if these domains cannot be closed statically.
