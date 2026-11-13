# Asset providers

Providers implement `Kotonoha::AssetProvider`. The game uses the public API;
methods ending in `Asset` are format implementation points.

| Operation | Public API |
| --- | --- |
| Texture (renderer cache) | `texture(renderer, id)` |
| RGBA Surface | `loadSurface(id)` |
| PCM audio + format | `loadAudio(id)` |
| UTF-8 text | `loadTextUtf8(id, encoding)` |
| Raw decompressed asset bytes, no text conversion | `read(id)` |

`contains`, `list`, `openStream` and `localPath` complement the interface.
Use the public API to preserve overlays, original selection, and cache.
The renderer is required for textures; surfaces and raw do not need it.
Available image/audio formats depend on the provider and codecs.

Text recognizes UTF-8, UTF-16LE/BE and UTF-32LE/BE BOMs. Without a BOM, the
encoding is explicit (default `UTF-8`; examples: `CP932`, `CP1252`, `UTF-16LE`).
Do not treat bytecode, binary tables, or mixed-encoding scripts as text:
in those cases the parser keeps receiving `read(id)`.
Conversions use the `ConvertEncoding` policy: invalid sequences become `?`.

## CMake modules

All providers live under `Kotonoha::Providers::`.

| Folder | Target | Provider / format dependency |
| --- | --- | --- |
| `cmp` | `Kotonoha::ProviderCMP` | `Kotonoha::Providers::SummerAssets` (Summer `.cmp`) |
| `adv` | `Kotonoha::ProviderADV` | `Kotonoha::Providers::SnowAssets` (Overflow `.dat`) |
| `gpk` | `Kotonoha::ProviderGPK` | `Kotonoha::Providers::SchoolDaysAssets`, GPK reader and retail mount |
| `xp3` | `Kotonoha::ProviderXP3` | `Kotonoha::Providers::KirikiriAssets` |
| `pak` | `Kotonoha::ProviderPAK` | `Kotonoha::Providers::LucaSystemAssets`, CZ codec |
| `reallive` | `Kotonoha::ProviderRealLive` | `Kotonoha::Providers::RealLiveAssets`; `RealLiveFormats` |
| `rpa` | `Kotonoha::ProviderRPA` | `Kotonoha::Providers::DdlcAssets`; `DdlcFormats` |
| `amagami` | `Kotonoha::ProviderAmagami` | `Kotonoha::Providers::AmagamiAssets`; `AmagamiFormats` |
| pmail | `Kotonoha::ProviderPmail` | `Kotonoha::Providers::PmailAssets` (Pmail archives) |
| `zero` | `Kotonoha::ProviderZero` | `Kotonoha::Providers::ZeroDiscAssets`; `ZeroNoTsukaimaFormats` |

The CMP, ADV, GPK, XP3 and PAK modules can be added directly:

```cmake
add_subdirectory(path/to/Kotonoha/modules/providers/cmp providers/cmp)
target_link_libraries(MyGame PRIVATE Kotonoha::ProviderCMP)
```

```cpp
#include <SummerRadish/SummerAssets.hpp>
Kotonoha::Providers::SummerAssets assets(dataDirectory);
auto raw = assets.read("INI/CONFIG.INI");
auto text = assets.loadTextUtf8("INI/CONFIG.INI", "UTF-16LE");
```

The RealLive, RPA, Amagami and Zero adapters still depend on the respective
format targets defined by the game projects. They do not include the graphics
runtime, but they are not self-contained packages to copy in isolation.
Filesystem and generic disk providers remain in the engine core.