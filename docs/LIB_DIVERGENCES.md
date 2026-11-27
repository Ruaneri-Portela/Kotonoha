# Shared library parity decisions — 2026-11-15

**English (US)** | [Português (Brasil)](LIB_DIVERGENCES.pt-BR.md)

Scope: Windows L4 synchronization with upstream/dev `fdd0b041`. The six
files below diverged at the review baseline `661312c`. The original diff,
file-by-file Git grep evidence and exact restoration proposal are retained
outside Git in `build/_sync-divergencias-lib.md`.

All six changes were **BEHAVIOR-CHANGING**, not additive: the local version
removed callable interfaces or changed results, costs or rendering. No
School Days-specific requirement justified overriding the common library.
Rather than adding a permanent game-specific fork, each file was restored
**byte-for-byte to upstream/dev**, in an individual local commit.

| File | Original class and reason in `lib/` | Users, including other ports | Existing verification | Action and remaining risk |
| --- | --- | --- | --- | --- |
| `lib/include/Kotonoha/components/SubtitleScene.hpp` | **Behavior-changing**: removed two public cue accessors; shared subtitle API | Amagami and Snow refer to `SubtitleScene`; School Days subclasses it. `git grep` found no direct uses of the removed accessors in `games/` or `modules/` | `Kotonoha.Presentation` covers subtitles, **not the accessor API directly** | Restored upstream in `a354496`. Existing callers regain ABI/source contract; no targeted accessor assertion yet |
| `lib/src/components/SubtitleScene.cpp` | **Behavior-changing**: replaced ASS glyph/texture pool, lost cache invalidation, font registration and some presentation logic; generic compositor | Amagami, Snow, School Days | `Kotonoha.Presentation` subtitle/rasterization tests; **no performance/cache stress test**, no real GPU QA | Restored upstream in `9ba0dc4`. Rendering and caching policy no longer forked; licensed-media and GPU behavior remain unverified |
| `lib/src/components/VNBacklog.cpp` | **Behavior-changing**: removed viewport entry culling and changed vertical layout calculation; shared backlog | Example, Amagami, Kirikiri, Luca System and PMAIL (other ports override some methods) | `Kotonoha.Presentation`, `Kotonoha.ComponentHooks`, Example engine tests; **no large-history rendering benchmark** | Restored upstream in `0588a9f`. Offscreen culling and measured text spacing preserved for other games |
| `lib/src/core/AssetOverlay.cpp` | **Behavior-changing**: dropped acceptance of an existing absolute directory when discovery fails; generic overlay infrastructure | DDLC RPA provider and School Days GPK overlay | `Kotonoha.Directories`, `Kotonoha.AssetProvider`, School Days synthetic asset tests; **no dedicated network-volume fallback test** | Restored upstream in `563d648`. Absolute-path fallback reinstated without changing signatures |
| `lib/include/Kotonoha/localization/Translation.hpp` | **Behavior-changing**: removed public `text(string section, string original)` overload; shared localization API | Amagami, DDLC, Example, Kirikiri, PMAIL, RealLive, Snow, Summer and Zero use `text(int route,...)`; no string-section calls found in current `games/` or `modules/` | Example translation test verifies integer-route overload; **no string-section-specific assertion** | Restored upstream in `8b50862`. Both overloads available; risk of external clients failing to compile is removed |
| `lib/src/localization/Translation.cpp` | **Behavior-changing**: removed section lookup, `nodo` fallback and global-source tracking; generic translation implementation | Same integer-route consumers listed above; public string-section API had no callers in current ports | Integer-route tests; **section fallback not separately exercised** | Restored upstream in `6428e41`. Original lookup and fallback order restored; add a dedicated regression if future edits touch the API |

No new option or School Days-only override is required: the desired behavior
for these six shared files **is the upstream default**. An option would
reintroduce avoidable divergence and would require a new game-specific
behavior whose necessity has not been demonstrated.

## Other ports and remaining boundaries

The CMake game matrix is in `buildsystems/cmake/KotonohaGames.cmake`; each
port is selected with `KOTONOHA_BUILD_<NAME>=ON`. The Windows
Release-only compile of non-School Days ports is documented in the local
verification log and `build/_sync-lib-otherports-*.log`.
Successful compilation proves symbol/signature compatibility; it cannot
prove gameplay, visual quality, saves, codecs or non-Windows parity.

The upstream `StoragePaths.cpp` is unchanged by the sync. Its Desktop
implementation places save data under a namespaced application directory,
while the original upstream `Kotonoha.Directories` test asserts a bare
user-data root. Keep the local test checking the implementation's actual
namespace unless the upstream test and code are reconciled together. The
Windows School Days `settings.json` exception belongs in
`games/school-days/src/main.cpp`, not in the shared storage API.

### Additional compatibility finding from compiling other ports

Compiling the ten other Windows ports also revealed a separate removed
contract in `lib/include/Kotonoha/core/Runtime.hpp`: Kirikiri,
Luca System and PMAIL still override `dialogueText() const` and
`dialogueSpeaker() const`, while the synchronized base class no longer
declared them (MSVC C3668). This is outside the six files above. Commit
`dcc90ac` restores both virtual methods with the upstream's empty-string
defaults, retaining the School Days-specific additions. It is an
**ADDITIVE** fix for existing runtimes, with no changed default behavior.
The Windows multi-port compile log contains the first failure and retest.
