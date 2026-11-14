# Game ports

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Kotonoha supports multiple engines and game-specific runtimes. **Support is not complete retail parity:** check each port's limitations before reporting a title as fully playable. Use legally obtained local assets in `games/<slug>/game-assets/` or choose a source in its launcher.

| Game / family | Build option | Target | Documentation |
| --- | --- | --- | --- |
| Example | `KOTONOHA_BUILD_EXAMPLE` | `Kotonoha` | [Readme](example/README.md) |
| Amagami | `KOTONOHA_BUILD_AMAGAMI` | `Amagami` | [Readme](amagami/README.md) |
| Zero no Tsukaima | `KOTONOHA_BUILD_ZERO` | `ZeroNoTsukaima` | [Readme](zero-no-tsukaima/README.md) |
| Snow Radish Vacation!! | `KOTONOHA_BUILD_SNOW` | `SnowRadish` | [Readme](snow-radish-vacation/README.md) |
| Summer Radish Vacation!! | `KOTONOHA_BUILD_SUMMER` | `SummerRadish` | [Readme](summer-radish-vacation/README.md) |
| Doki Doki Literature Club! | `KOTONOHA_BUILD_DDLC` | `DokiDoki` | [Readme](doki-doki-literature-club/README.md) |
| School Days HQ | `KOTONOHA_BUILD_SCHOOL_DAYS` | `SchoolDays` | [Readme](school-days/README.md) |
| RealLive | `KOTONOHA_BUILD_REALLIVE` | `RealLive` | [Readme](reallive/README.md) |
| Kirikiri2 / KirikiriZ | `KOTONOHA_BUILD_KIRIKIRI` | `Kirikiri` | [Readme](kirikiri/README.md) |
| LucaSystem | `KOTONOHA_BUILD_LUCA_SYSTEM` | `LucaSystem` | [Readme](luca-system/README.md) |
| Pure Mail | `KOTONOHA_BUILD_PMAIL` | `Pmail` | [Readme](pmail/README.md) |

Build one port by enabling its option: `cmake -S . -B build -DKOTONOHA_BUILD_REALLIVE=ON`, then `cmake --build build --target RealLive`. For tests use `ctest --test-dir build --output-on-failure`.

Files named `PORT_STATUS.md` and dated notes in game `docs/` directories retain development and reverse-engineering evidence. They may include older observations that later revisions superseded. For an accessible summary in both languages, start at each game's README.

[Documentation index](../docs/README.md) · [Build guides](../docs/BUILD_GUIDES.md)
