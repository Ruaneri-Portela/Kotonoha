# Repository organization and license audit

**English (US)** | [Português (Brasil)](LICENSING.pt-BR.md)

Audit snapshot: **2026-11-22**, `dev`. This is a source/repository audit,
not a legal opinion or a claim that every commercial game/OS was tested.

## Scope and organization

- Shared C++17 engine in `lib/`, VMs in `modules/vms/`, format providers in
  `modules/providers/`, game-facing runtimes in `games/`.
- The current source tree contains **16 game applications (15 ports + Example)**,
  plus the multi-game launcher. Use `games/README.md` and the build manifest
  instead of hard-coded stale counts.
- Build and packaging logic lives in `buildsystems/`; `tools/` hosts validation.
- Keep historical `PORT_STATUS.md` and dated investigations intact. Update
  current guides in English and Brazilian Portuguese together.
- Preserve source URLs, license texts, and contributor attribution. An external
  implementation referenced for interoperability is **not** automatically
  licensed for copy/paste into Kotonoha.

## Findings and actions

| Priority | Finding | Action / remaining verification |
| --- | --- | --- |
| High | Repository root lacked an explicit license even though `lib/LICENSE` contains an MIT grant | Add root `LICENSE` for original Kotonoha code; third-party exceptions in `THIRD_PARTY_NOTICES.md` |
| High | The macOS `.app` CPack branch copied the whole bundle and could inadvertently include locally supplied `game-assets` | Exclude `game-assets` by default in both bundle and non-bundle packaging; retain explicit opt-in only for privately authorized packages |
| High | Android Gradle automatically bundled local retail `game-assets` in APKs | Make `-PbundleGameAssets=false` the default, always exclude retail files in `dist`, and stage baseline legal notices |
| High | ZIPs did not explicitly install source license/third-party notices | Include baseline notices in every CPack game component, including `.app/Contents/Resources/Legal`. **Before public release**, assemble license texts for every actually bundled third-party binary |
| Medium | `.gitignore` contained concatenated debug rules | Separate patterns; check tracked files for accidentally committed secrets or retail archives |
| Medium | Counts of applications/ports in active README/AGENTS/build guides were outdated | Align guides with the current 16-app manifest |
| Medium | `assets/Anime4K/SOURCE.txt` only describes three files despite a larger shader directory | Clarify the provenance gap; verify all shader revisions against their upstream before redistributing modified variants |
| Needs owner verification | `assets/SPLASH.PNG`, two icon files and `lib/tests/fixtures/SvgTestFont.ttf` have no clear standalone provenance record in the audited files | Confirm authorship, font terms, and binary distribution rights; do not imply approval merely from a filename |
| High | The [rlvm](https://github.com/eglaysher/rlvm) and [ponscripter-fork](https://github.com/sekaiproject/ponscripter-fork) reference repositories are identified as GPL-3.0 and GPL-2.0 | Do not import GPL implementation files into MIT-only modules without a licensing/compliance decision; the separately vendored `libreallive` subset carries individual MIT headers and a bundled notice. Source-level provenance of all ports remains to be reviewed |
| Ongoing | The N2System/LucaSystem ports are not proven compatible with all titles, save formats or platforms | Keep verification checklists in issues #83 and #75 until evidence exists |

## Dependency release checklist

1. Identify **actual** linked libraries and runtime features for Windows,
   Linux, macOS, iOS, Android and WebAssembly. A `vcpkg.json` dependency
   declaration is not a final binary bill of materials.
2. Include the applicable notices for SDL, SDL_ttf, Dear ImGui, FFmpeg, libass,
   CPython and transitive dependencies that are distributed with the package.
   Inspect FFmpeg configure options separately for GPL or nonfree features.
3. Validate `cpack` output on each platform, not merely its CMake syntax.
   Confirm that `game-assets/` is absent by default, license files are
   present, and test-only fonts or retail fixtures are not accidentally bundled.
4. Maintain public source attributions for Anime4K, ArtCNN and open-amagami
   portions, including modifications and license texts.
5. Run `python3 tools/ValidateRepositoryHygiene.py` before publishing.

See [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) for the inventory.
