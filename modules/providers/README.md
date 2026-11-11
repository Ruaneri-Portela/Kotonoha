# Providers de assets

Os providers implementam `Kotonoha::AssetProvider`. O jogo usa a API pública;
os métodos terminados em `Asset` são pontos de implementação dos formatos.

| Operação | API pública |
| --- | --- |
| Textura (cache do renderer) | `texture(renderer, id)` |
| Surface RGBA | `loadSurface(id)` |
| Áudio PCM + formato | `loadAudio(id)` |
| Texto UTF-8 | `loadTextUtf8(id, encoding)` |
| Bytes do asset descompactado, sem conversão textual | `read(id)` |

`contains`, `list`, `openStream` e `localPath` complementam a interface.
Use a API pública para preservar overlays, seleção do original e cache.
O renderer é obrigatório para texturas; surfaces e raw não precisam dele.
Os formatos de imagem/áudio disponíveis dependem do provider e dos codecs.

Texto reconhece BOM UTF-8, UTF-16LE/BE e UTF-32LE/BE. Sem BOM, a codificação
é explícita (padrão `UTF-8`; exemplos: `CP932`, `CP1252`, `UTF-16LE`).
Não trate bytecode, tabelas binárias ou scripts com codificação mista como
texto: nesses casos o parser continua recebendo `read(id)`.
Conversões usam a política de `ConvertEncoding`: sequências inválidas viram `?`.

## Módulos CMake

Todos os providers vivem em `Kotonoha::Providers::`.

| Pasta | Alvo | Provider / dependência de formatos |
| --- | --- | --- |
| `cmp` | `Kotonoha::ProviderCMP` | `Kotonoha::Providers::SummerAssets` (Summer `.cmp`) |
| `adv` | `Kotonoha::ProviderADV` | `Kotonoha::Providers::SnowAssets` (Overflow `.dat`) |
| `gpk` | `Kotonoha::ProviderGPK` | `Kotonoha::Providers::SchoolDaysAssets`, leitor GPK e montagem retail |
| `xp3` | `Kotonoha::ProviderXP3` | `Kotonoha::Providers::KirikiriAssets` |
| `pak` | `Kotonoha::ProviderPAK` | `Kotonoha::Providers::LucaSystemAssets`, codec CZ |
| `reallive` | `Kotonoha::ProviderRealLive` | `Kotonoha::Providers::RealLiveAssets`; `RealLiveFormats` |
| `rpa` | `Kotonoha::ProviderRPA` | `Kotonoha::Providers::DdlcAssets`; `DdlcFormats` |
| `amagami` | `Kotonoha::ProviderAmagami` | `Kotonoha::Providers::AmagamiAssets`; `AmagamiFormats` |
| `zero` | `Kotonoha::ProviderZero` | `Kotonoha::Providers::ZeroDiscAssets`; `ZeroNoTsukaimaFormats` |

Os módulos CMP, ADV, GPK, XP3 e PAK podem ser adicionados diretamente:

```cmake
add_subdirectory(path/to/Kotonoha/modules/providers/cmp providers/cmp)
target_link_libraries(MeuJogo PRIVATE Kotonoha::ProviderCMP)
```

```cpp
#include <SummerRadish/SummerAssets.hpp>
Kotonoha::Providers::SummerAssets assets(dataDirectory);
auto raw = assets.read("INI/CONFIG.INI");
auto text = assets.loadTextUtf8("INI/CONFIG.INI", "UTF-16LE");
```

Os adaptadores RealLive, RPA, Amagami e Zero ainda dependem dos respectivos
alvos de formatos definidos pelos projetos dos jogos. Eles não incluem o
runtime gráfico, mas não são pacotes autossuficientes para copiar isoladamente.
Providers de filesystem e disco genérico permanecem no núcleo do motor.

## Xcode

`ruby Xcode/UpdateGameTargets.rb` sincroniza os sources dos módulos e seus
include paths com os targets dos jogos (incluindo o smoke target DDLC).
O script é idempotente. No Xcode cada app compila o módulo que utiliza;
no CMake cada módulo é uma biblioteca separada.
