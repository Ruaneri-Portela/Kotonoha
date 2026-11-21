# Shared Debug, Translation and Dubbing Contract

All Kotonoha applications use the *same* Engine inspector and applets. A game
runtime must never implement its own asset browser, translation editor or
recording UI. Game-specific code belongs in a Runtime adapter and an
AssetProvider adapter.

## Contract

1. **AssetProvider** exposes logical, stable IDs through `listAsset(prefix)`,
   `containsAsset`, `readAsset`, `loadSurfaceAsset` or `loadAudioAsset`.
   The engine owns overlay read precedence, original-versus-effective export,
   preview, caching and safe raw dump paths. Archives should enumerate entries
   from their metadata instead of reading all payloads.
2. **Runtime::debugNodes** describes native script/scenario identifiers with
   references into the same AssetProvider. `debugJumpToNode` must reject
   unsupported targets and never treat data tables as executable scripts.
   `debugNodeCatalogKey` changes when a script catalog needs refreshing.
   Ports using VNManager directly may rely on its existing node adapter.
3. **Runtime::translationSources** supplies original untranslated keys by
   route. `activeTranslationKeys` exposes speaker, dialogue and active
   choices. Native renderers use `Engine::GetTranslation().text(route, key)`,
   with the same route as their catalog and Debug UI.
4. **Runtime::debugSceneAudio** returns `{providerId, character, originalText}`.
   Report character/text only when a specific voice is known; music, sound
   effects and speculative recent audio do not belong to a character.
   `debugOriginalAudio` is a fallback only for audio outside AssetProvider.
   `debugOverlayAssetChanged` invalidates caches when a dub changes.
5. **Shared UI**: F10 inspects native or VNManager nodes, browses any provider,
   previews resources and dumps original/effective raw bytes. F7 edits text
   and follows native route changes. The dubbing applet enumerates audio IDs
   on request and offers original/overlay preview and nondestructive edits.

## Runtime mapping

| Game | Native Runtime | Note |
| --- | --- | --- |
| Amagami | AmagamiRuntime | PS2 disc and indexed virtual voice/music/SE IDs |
| DDLC | Ddlc::GameRuntime | Ren'Py labels, text and active mixer channels |
| Example | VNManager fallback | No game-native scripting catalog |
| Katawa Shoujo | Ddlc::GameRuntime | Shares Ren'Py runtime with DDLC |
| Kirikiri | KirikiriRuntime | KAG/SCN scripts, scene visuals and voice callbacks |
| LucaSystem | LucaSystemRuntime | PAK scenes and instruction-level debugger |
| Miss Each Other (MEO) | SummerRuntime | MEO XML scenes and scripts |
| N2System | N2Runtime | Native NsScript chapters or Ponscripter delegate |
| Pure Mail | PmailRuntime | Script PAK index and original VM strings |
| Ponscripter | PonscripterRuntime | Native PONscripter VM integration |
| RealLive | RealLiveRuntime | Scenario/voice integration |
| School Days | SchoolDaysRuntime | Visual/audio scene integration |
| Snow Radish Vacation | SnowRuntime | Script object routes, voices and choices |
| Summer Radish Vacation | SummerRuntime | Script/scene and translation integration |
| Zero no Tsukaima | ZeroRuntime | Disc script scenes, voice IDs and choices |

## Validation

```sh
python3 tools/check_debug_bridge.py
python3 tools/check_port_debug_contracts.py
g++ -std=c++17 -Wall -Wextra -Werror -Ilib/include \
    lib/tests/DebugAssetCatalogTests.cpp -o /tmp/kotonoha-debug-assets
/tmp/kotonoha-debug-assets
```

The GitHub *Game port startup and compatibility contracts* workflow runs
these source-level checks and the native Luca debug-catalog unit test.
**Passing those checks does not mean the full C++ engine, games or audio codecs
have compiled or run**. Full CMake/Xcode builds and smoke tests with
legitimately obtained game data remain essential. Some retail formats or
voice streams may still be unsupported; listing an ID never implies a
successful decode or working dubbing replacement.

When adding a new game to `ProjectManifest.json`, update
`tools/check_port_debug_contracts.py` with its Runtime adapter mapping.
The test intentionally fails if any registered game lacks a declared
integration path.
