# School Days HQ — commercial test build checklist

This checklist is the release gate for the current School Days target. Generic
physical display/upscaling mechanisms belong to `KotonohaEngine`; authored
School Days UI/aspect/profile/persistence policy stays under `games/school-days`.

## Player-facing blockers

- [x] Windows player build uses the GUI subsystem; no console/log terminal is
      allocated beside the game for ordinary users.
- [x] BackLog and Choice presenters use the same School Days subtitle typography
      helper/font family, outline and shadow treatment as the narrative subtitle
      presentation.
- [x] Player-facing anime upscaler presets are selectable at runtime: Off,
      Anime Light (`ArtCNN C4F16`), Anime Clean (`ArtCNN C4F16 DS`), Anime
      Quality (`ArtCNN C4F32`) and Legacy (`Anime4K B-S`). The commercial-test
      default is Anime Light; Anime4K remains a compatibility preset rather than
      an engine-wide assumption.
- [x] ArtCNN fragment shaders are pinned to the pre-compute revision
      `d842d656518df4c78062c4411b116264aeff243c`, shipped with MIT attribution,
      and run through the existing generic OpenGL/mpv-compatible backend.
- [x] Player-facing Display Options can change/disable the upscaler without a
      renderer restart. The control crosses the exported runtime boundary, bumps
      the generic texture-cache revision and therefore updates already-cached
      static images as well as future frames.
- [x] Upscaler output preserves source alpha byte-for-byte in the OpenGL runtime
      gate (`max_alpha_diff=0`) for Legacy, Light, Clean, Quality and live
      Legacy->Clean hot-swap.
- [x] ABC mouth/eye overlays remain composited with the authored base at source
      resolution before any AI upscaler. Retail A01/A02/E00 fixtures read directly
      from GPKs pass the ownership/hotfix and edge-seam gates; sparse transparent
      mouth textures are never neural-upscaled independently.
- [x] Aspect preference is explicit and game-owned (Auto / 4:3 / 16:9).
- [x] Window mode supports Windowed / Borderless / Exclusive fullscreen.
- [x] Original General-page WINDOW/FULL is synchronized with the modern
      physical display state: FULL => borderless fullscreen, WINDOW => restored
      persisted window size. Exclusive remains an advanced Display-page choice.
- [x] Resolution list comes from the selected display; fullscreen can adapt to
      the user's current/native display mode.
- [x] Window size/resolution/aspect/upscaling settings are serialized in the
      executable-relative `Kotonoha.ini` on orderly exit and loaded on startup.
- [x] Audio/gameplay options remain game-owned in `Config.DAT`; shutdown flush
      was exercised and produces a changed persisted Config.DAT when an original
      option is changed.
- [x] Fresh-user state and completed-user state are both validated.

Implementation is complete for the player-facing blockers above. The remaining
items in **Final smoke** are release-candidate observation checks rather than
missing mechanisms.

<!-- Historical pre-implementation wording retained below only in git history. -->
<!--
      Window size/resolution/aspect/upscaling settings survive orderly exit and
      restart.
-->

## Completion / saves

- [x] Runtime A/B/C/D probe against original v1.02 proved that
  `GlobalFlag.DAT` is the global progression authority. A completed GlobalFlag
  alone must preserve AllClear/Replay even with no `SaveFile*.DAT` files.
- [x] Ordinary `SaveFile*.DAT` files are LOAD/PlayData checkpoints and must not be
  treated as the completed-mode latch.
- [x] Original-runtime witness: a fresh install state with an empty `Save/`
  starts in the fresh/default Title and has REPLAY disabled.
- A local QA package may optionally seed the certified completed GlobalFlag and
  ending checkpoints, but the public/default package should not silently start
  completed unless the release explicitly chooses that experience.

## Distribution structure

Target Windows layout:

```text
SchoolDaysHQ/
  SchoolDays.exe
  libKotonoha.dll
  SDL3.dll
  SDL3_ttf.dll
  ass-9.dll
  avcodec-*.dll
  avformat-*.dll
  avutil-*.dll
  swresample-*.dll
  swscale-*.dll
  ...other required runtime DLLs
  Kotonoha.ini
  Config.DAT
  assets/
  route/
    school-days-hq.ktnroute
  Packs/
  Save/
```

The packaging step must be reproducible. Proprietary/local GPK copying must be
an explicit opt-in for private QA packaging rather than an accidental default.

## Final smoke

- Title -> New Game -> first choice -> next physical scene.
- Choice / BackLog fonts verified at 100%, 150%, 200% display scaling.
- Save -> advance -> Load -> exact state return.
- Windowed -> Borderless -> Exclusive -> Windowed with several resolutions.
- Fullscreen on a non-1080p monitor uses a valid selected/native mode.
- Upscaler Off / Light / Clean / Quality / Legacy selection survives restart,
  hot-swaps during gameplay and does not change authored UI geometry or ABC alpha.
- 1x/2x/4x/12x/24x transport remains correct.
- Reach an ending -> EndRoll -> Title -> Replay/PlayData.
- [x] Fresh Save/ directory: default Title, Replay locked.
- [x] Completed GlobalFlag with no SaveFiles: completed Title/AllClear visual and
      Replay available. This was runtime-probed separately from the 21 ending
      checkpoint SaveFiles.
