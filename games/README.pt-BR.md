# Ports de jogos

[English (US)](README.md) | **Português (Brasil)**

Kotonoha oferece múltiplos interpretadores e runtimes específicos. **Suporte não significa fidelidade integral ao jogo comercial:** confira as limitações antes de anunciar que um título inteiro é jogável. Use assets locais legalmente obtidos em `games/<slug>/game-assets/` ou escolha a origem no launcher.

| Jogo / família | Opção CMake | Alvo | Documentação |
| --- | --- | --- | --- |
| Example | `KOTONOHA_BUILD_EXAMPLE` | `Kotonoha` | [Guia](example/README.pt-BR.md) |
| Amagami | `KOTONOHA_BUILD_AMAGAMI` | `Amagami` | [Guia](amagami/README.pt-BR.md) |
| Zero no Tsukaima | `KOTONOHA_BUILD_ZERO` | `ZeroNoTsukaima` | [Guia](zero-no-tsukaima/README.pt-BR.md) |
| Snow Radish Vacation!! | `KOTONOHA_BUILD_SNOW` | `SnowRadish` | [Guia](snow-radish-vacation/README.pt-BR.md) |
| Summer Radish Vacation!! | `KOTONOHA_BUILD_SUMMER` | `SummerRadish` | [Guia](summer-radish-vacation/README.pt-BR.md) |
| Doki Doki Literature Club! | `KOTONOHA_BUILD_DDLC` | `DokiDoki` | [Guia](doki-doki-literature-club/README.pt-BR.md) |
| School Days HQ | `KOTONOHA_BUILD_SCHOOL_DAYS` | `SchoolDays` | [Guia](school-days/README.pt-BR.md) |
| RealLive | `KOTONOHA_BUILD_REALLIVE` | `RealLive` | [Guia](reallive/README.pt-BR.md) |
| Kirikiri2 / KirikiriZ | `KOTONOHA_BUILD_KIRIKIRI` | `Kirikiri` | [Guia](kirikiri/README.pt-BR.md) |
| LucaSystem | `KOTONOHA_BUILD_LUCA_SYSTEM` | `LucaSystem` | [Guia](luca-system/README.pt-BR.md) |
| Pure Mail | `KOTONOHA_BUILD_PMAIL` | `Pmail` | [Guia](pmail/README.pt-BR.md) |

Compile um port habilitando sua opção: `cmake -S . -B build -DKOTONOHA_BUILD_REALLIVE=ON`, depois `cmake --build build --target RealLive`. Para testes, use `ctest --test-dir build --output-on-failure`.

Arquivos `PORT_STATUS.md` e notas datadas nas pastas `docs/` dos jogos preservam evidências de desenvolvimento e engenharia reversa. Algumas observações antigas podem ter sido superadas. Para resumos claros em ambos os idiomas, comece pelo README de cada jogo.

[Índice geral](../docs/README.pt-BR.md) · [Compilação](../docs/BUILD_GUIDES.pt-BR.md)
