# Third-party notices and redistribution boundary

This file documents third-party components known to be used by Kotonoha.
It is **not** a license grant for those components or for any commercial game.

## Original Kotonoha code

Original Kotonoha code is distributed under the MIT License in [LICENSE](LICENSE)
(and the historical copy in [lib/LICENSE](lib/LICENSE)). A copyright or SPDX
header on a file must not be used to relicense code taken from elsewhere.

## Third-party source files checked into this repository

| Material | Upstream | License/notice tracked here |
| --- | --- | --- |
| Anime4K shaders under `assets/Anime4K/` | https://github.com/bloc97/Anime4K | [MIT text](assets/Anime4K/LICENSE-Anime4K.txt), [source record](assets/Anime4K/SOURCE.txt) |
| ArtCNN shaders under `assets/ArtCNN/` | https://github.com/Artoriuz/ArtCNN | [MIT text](assets/ArtCNN/LICENSE-ArtCNN.txt), [source record](assets/ArtCNN/SOURCE.txt) |
| open-amagami-derived provider portions | https://github.com/hokagocafe/open-amagami | [MIT text](modules/providers/amagami/third_party/open-amagami-LICENSE.txt) |
| MIT-licensed libreallive parser subset in `games/reallive/vendor/libreallive/` | https://github.com/eglaysher/rlvm | [MIT copyright notice](games/reallive/vendor/libreallive/LICENSE), [source provenance](games/reallive/vendor/libreallive/ORIGIN.md); preserve individual headers |

Keep original notices and attribution when modifying or redistributing these
files. The Anime4K source record names only a subset of the shaders at the
pinned commit: **a full upstream revision/hash comparison for every variant
has not been completed**.

## Git submodules and runtime dependencies

SDL3, SDL3_ttf and Dear ImGui are tracked as **Git submodules**, not relicensed
by Kotonoha. They are resolved through `.gitmodules` and have their own
copyright and license notices. FFmpeg, libass, CPython and dependencies from
`vcpkg.json` or the host system similarly retain their own terms.

For every binary release, inspect the **exact versions and build features**
actually shipped. In particular, FFmpeg licensing can depend on configured
codecs and GPL/nonfree options. Bundle all applicable licenses, copyright
notices, and any additional attribution or source offers required by the
chosen third-party licenses. The notices included in Kotonoha ZIP components
are a **baseline**, not proof that all dynamically bundled dependencies have
been cleared.

## References that require additional license caution

Some original engine implementations are **references**, not vendored
dependencies. GitHub repository metadata reports
[rlvm](https://github.com/eglaysher/rlvm) as **GPL-3.0** and
[ponscripter-fork](https://github.com/sekaiproject/ponscripter-fork) as
**GPL-2.0**. Do **not** import implementation files from these projects into
MIT-only Kotonoha modules without a compatible license decision and compliance
plan. The separately identified `libreallive` parser subset contains its own
MIT notices; the broader rlvm repository's GPL does not disappear.

[NitroSharp](https://github.com/CommitteeOfZero/nitrosharp) and
[LuckSystem](https://github.com/wetor/LuckSystem) are identified as MIT
upstreams; retain their notices if any code is copied.
[Kirikiroid2Yuri](https://github.com/YuriSizuku/Kirikiroid2Yuri) reports
an unclassified/mixed repository license in GitHub metadata: determine the
license of **each source file and dependency** before copying anything.
The presence of an external link alone is not a license violation and must
not be used to remove required attribution. The source-level provenance of
every Kotonoha implementation was **not fully established** by this audit.

## Material requiring provenance confirmation

- Splash and platform icons are generated into `design/generated/` by
  `tools/generate_brand_assets.py` from `design/tokens.json`. Their source is
  in-repo, so the former `assets/SPLASH.PNG` / `assets/icon.*` provenance
  review no longer applies; those legacy binaries are being removed.
- `lib/tests/fixtures/SvgTestFont.ttf`: test-only font; origin and embedding
  permission still need to be recorded. Do not copy it into a player bundle.
- Synthetic game test fixtures should remain traceable to their generators;
  commercial game screenshots, sound, scripts or archive contents must not be
  committed as test fixtures without permission.

These items are **not proven infringing**. They are flagged as unverified,
rather than deleted or incorrectly described as openly licensed.

## Commercial game data, titles and external references

Game names, logos, scripts, media and other retail assets belong to their
respective owners. Locally provided `games/<slug>/game-assets/` content must
not be redistributed by CI or default desktop packaging. Links to third-party
open-source implementations are attribution/reference links only; they do not
confer permission to reuse proprietary assets or to remove required notices.

See the bilingual audit guide in [docs/LICENSING.md](docs/LICENSING.md) and
[docs/LICENSING.pt-BR.md](docs/LICENSING.pt-BR.md).
