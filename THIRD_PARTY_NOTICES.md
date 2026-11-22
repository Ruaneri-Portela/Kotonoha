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

## Material requiring provenance confirmation

- `assets/SPLASH.PNG`, `assets/icon.icns`, `assets/icon.ico`: owner/source
  and redistribution authorization should be recorded before a public release.
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
