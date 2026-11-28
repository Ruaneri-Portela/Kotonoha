# Embedded options artwork for School Days HQ (Windows and Android)

**English (US)** | [Português (Brasil)](README.pt-BR.md)

This directory contains public metadata and instructions only. The actual
user-supplied artwork remains outside Git. The preferred private source is
`games/school-days/game-assets/overlay/`; the previous
`games/school-days/game-assets/display-options/` source remains supported.

No PNGs from the commercial game or customized derivative artwork are
distributed in this repository. The custom Display Options page uses
user-supplied resources in `System/Option/` beneath that private directory. The
complete list, dimensions and behavior are documented in
[`PR_PARITY_CONSOLIDATION_2026-11-05.md`](../../PR_PARITY_CONSOLIDATION_2026-11-05.md).

The standard General/Sound CMAPs continue to come from the original game.
The Display CMAPs belong to the customized third page and must accompany
`Option_Display.png` and `Option_Display_Chip.png`. The 22 regions' bounds
are described in `display_options_controls.json` for reproducibility.

Windows and Android DEV/DIST compile all six approved Display resources into
the native binary when present at configuration time. The generator checks
their SHA-256 hashes. Set `KOTONOHA_SCHOOL_DAYS_DISPLAY_OVERLAY_SOURCE` to the
private source directory and `KOTONOHA_SCHOOL_DAYS_REQUIRE_EMBEDDED_OVERLAY=ON`
to fail if the inputs are missing. On Android, use Gradle properties
`schoolDaysDisplayOverlaySource` and `schoolDaysRequireEmbeddedOverlay=true`.

For authorized private QA only, the four approved user-edited General/Sound
PNGs can also be embedded using `KOTONOHA_SCHOOL_DAYS_EMBED_PRIVATE_OPTION_ART=ON`
or Android property `schoolDaysEmbedPrivateOptionArt=true`. Do not redistribute
those binaries without permission. No private input bytes are tracked in Git.

No installed overlay directory is required for the embedded UI. Existing
external overrides remain optional and take precedence over embedded resources;
the original retail packs supply the remaining resources.

Never patch, repack or replace the commercial `Packs/System.GPK` for this UI.
