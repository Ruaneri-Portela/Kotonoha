# Asset overlays and translation

**English (US)** | [Português (Brasil)](ASSET_OVERLAYS.pt-BR.md)

The shared `AssetProvider` API checks an optional override directory **before** original game data for reads, decoding, listings, existence checks, and streaming paths. An overlay never rewrites the installation.

```sh
Game --asset-overlay /path/to/overrides
Game --no-asset-overlay
```

The launcher also provides **Game → Asset overlay**. Override paths must match logical asset IDs, such as `bgm/1.ogg` (DDLC), `script/START.KS` (Zero), or `g00/name.g00` (RealLive). Filenames are normalized for ASCII case and path separators; ambiguous case-only duplicates, path traversal, and escaping symlinks are rejected. Remount after adding new paths. Already decoded resources may remain cached until invalidated.

## Translation and input

- The translation editor updates active text, choices, and subtitle scenes without re-entering a scene. Export with `--dump-translation FILE`, which initializes the selected game and exits without progressing through the story.
- DDLC's optional VNX translation packages may be supplied as an overlay; keep the original installation separate. Loose scripts and patch archives take precedence over base data.
- Common inputs include Escape/Android Back/right click for back and B/Page Up/wheel up for backlog. Ports can override actions for native menus.
- Rendering caches avoid reprocessing the same source texture, and UI/subtitle glyphs remain at native text resolution.

**Developer contract:** Call the public `AssetProvider` methods (`read`, `loadSurface`, `texture`, `loadAudio`, etc.) instead of bypassing overlays with format-specific methods. `FileResolver` handles platform-specific storage paths. See [asset providers](../modules/providers/README.md), [directory policy](DIRECTORY_POLICY.md), and [engine settings](ENGINE_SETTINGS.md).
