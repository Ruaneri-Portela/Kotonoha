# Kirikiri2 / KirikiriZ — support summary

**English (US)** | [Português (Brasil)](STATUS.pt-BR.md)

**Implemented/tested scope:** Standard XP3 v1/v2 archives (chained/zlib indexes, segmented files, patch overlay precedence), TLG5/TLG6/PIMG images, clean-room PSB v2 + MDF binary parser (`PsbFile`), KAGEnv `.ks.scn` scenario runner (`ScnRunner` with PT-BR `VNX+/` localization, speaker nameplates `cn_name_*.png`, multi-channel voices, E-mote keyframe actions, savepoint snapshot persistence, and `00_01.ks`..`00_15.ks` chapter progression), native `VNX+/` UI screens (Title menu `title_cn@*`, Save/Load `saveload_cn@*`, Config `config_cn@*`, Backlog `backlog_cn@*`, Extra/Chapter select `extra_cn@*`, in-game message box/toolbar `window@help__*` and `qlpopup@*`), plain KAG3 scripts, and bounded TJS2 bootstrap evaluation are implemented.

**Limits:** Retail `.xp3` archives encrypted by third-party native protection filters (`PackinOne.dll`) are indexed without DRM circumvention; unencrypted overlays (`VNX+/`) and standard unpacked Kirikiri assets render directly.

## NEKOPARA_VOL0_PT-BR — retail evidence (2026-11-23)

Read-only source: `/Volumes/Nero 2TB/KotonohaAssets/NEKOPARA_VOL0_PT-BR`.
No commercial asset was copied into the repository. `PORT_STATUS.md` was not
modified.

### Hashes and sizes (evidence)

| File | Bytes | SHA-256 |
|---|---:|---|
| `data.xp3` | 191864049 | `6384cd1a312d64574339fe9066120bcd7cd997b9a4b0378ca4b1ddffe9d2cfbf` |
| `patch.xp3` | 152097 | `691bb011334dd13c9c1b01522c61fad8100bf7e847dad70d8387c7f49beaf8f1` |
| `data.xp3.sig` | 212 | `840670c2ec7365c28214ce5b1dfff616dbe33c9456a1fb5fb4e85abaee8ea05b` |
| `nekopara_vol0.exe` | 4338688 | `8f2d864eaedc9fcd31a31fcecd22fbed3d9299c0ea68f86781e7523ae2a0df23` |
| `plugin/PackinOne.dll` | 332288 | `8a2d7af0e53603020ab791e9ed676f2cd7fe5b7d0250acce26ff393db4576875` |

`data.xp3.sig` starts with `-- SIGNATURE - SHA256/PSS/RSA --` followed by two
base64 lines. `patch.xp3` has no `.sig` file in this installation.

### XP3 structure and protection

`KirikiriXp3` (the existing `Kotonoha::Providers::KirikiriAssets` provider)
opens both archives **without throwing**: the index is readable. That is not
the same as usable payloads.

- `data.xp3`: header index offset `0x17` is a 0-byte chained redirect
  (`flags=0x80`); the real index is at `0xb6e83d2` (`flags=0x01`, 71950 stored
  bytes -> 314966 decoded), yielding 1183 entries / 1183 segments (948 raw,
  235 zlib).
- `patch.xp3`: index at `0x250c5` (`flags=0x01`, 331 stored -> 960 decoded),
  yielding 4 entries / 4 raw segments.
- `info` flags: `data.xp3` has 1182/1183 entries with bit 31 set
  (`0x80000000`, encrypted payload); only the plaintext warning `.txt` has
  flags `0`. `patch.xp3` has 4/4 entries with `0x80000000`. Combined:
  **1186/1187 protected entries**.
- Payload check: the single readable entry begins with
  `57 61 72 6e 69 6e 67 3a` (`"Warning:"`); the hashed entries begin with
  high-entropy bytes and no PNG/TLG/OGG/PSB magic.
- `startup.tjs` (from `data.xp3`) is 316 bytes of compiled TJS2 bytecode
  (`TJS2100\0`), with the embedded diagnostic
  `startup failed: corrupted execution file`. There is no runnable source
  bootstrap and no KAG entry inside the XP3 (`kag=''`, `error=''`).
- The 16 visible scenarios all come from loose `VNX+/` files; **0** are
  XP3-only. `KirikiriXp3` therefore cannot supply the game's own title or
  scripts from these archives.

Exact retail-gated diagnostic:

```
[SKIP] protected XP3: 1186/1187 entries carry info flag 0x80000000 (encrypted payload); startup error: startup failed: corrupted execution file
```

### Achievable vs blocked

- **Achievable (real, passing test):** mount/index both XP3s; read the loose
  PT-BR overlay `VNX+/` (185 files: 166 PNG, 18 `.scn`, 1 `.ks`), load
  `VNX+/sysmenu.ks` (560 UTF-8 bytes), parse `VNX+/scn/scenelist.scn` (15
  chapter entries), and run `VNX+/scn/00_01.ks.scn` through `ScnRunner`
  language slot 2 (PT-BR dialogue decoded, 29 bytes in the first line).
- **Blocked:** 1:1 parity of the original packaged art, audio, movies, E-mote
  PSBs and scripts. Their XP3 payloads are encrypted by `PackinOne.dll`; the
  clean-room parser intentionally does not circumvent DRM. Without decrypted
  assets or the native Windows filter, Kotonoha can only present the `VNX+/`
  translation UI and scripts, not the original retail game.

### Retail-gated tests and verification

`KIRIKIRI_TEST_DATA` now feeds two tests with `SKIP_RETURN_CODE 77` and label
`real` (no false pass):

- `Kirikiri.RetailXp3Startup` — mounts `data.xp3` + `patch.xp3`, inspects the
  XP3's own `startup.tjs`, then tries the KAG startup. Skips with the exact
  protection diagnostic above.
- `Kirikiri.RetailVnxScripts` — loads the loose PT-BR scripts and verifies
  real dialogue from chapter `00_01.ks.scn`.

```
cmake -S . -B build -DKIRIKIRI_TEST_DATA="/Volumes/Nero 2TB/KotonohaAssets/NEKOPARA_VOL0_PT-BR"
ctest --test-dir build -R '^Kirikiri\.' -V
# with gate: Kirikiri.RetailXp3Startup = Skipped (77),
#            Kirikiri.RetailVnxScripts = Passed
cmake -S . -B build -DKIRIKIRI_TEST_DATA=""
ctest --test-dir build -R '^Kirikiri\.' -V
# without gate: both = Skipped (77)
```

Full `ctest --test-dir build -j8`: 214 tests, 97% passed; the 7 failures are
unrelated pre-existing items (3 `Kotonoha.*` synthetic tests and 4
`RealLive.Retail.*` "Not Run" tests whose binaries are absent in this build).
`python3 tools/ValidateBuildMetadata.py`: 16 CMake games; 34 existing Xcode
schemes; Xcode pending: none. `git diff --check`: clean.

[Port README](README.md) · [All ports](../README.md)
