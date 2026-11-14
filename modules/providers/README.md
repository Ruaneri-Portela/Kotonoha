# Asset providers

**English (US)** | [Português (Brasil)](README.pt-BR.md)

Providers implement `Kotonoha::AssetProvider` and expose the same access methods regardless of the archive/game format.

| Purpose | Public method |
| --- | --- |
| Cached texture | `texture(renderer, id)` |
| Decoded image surface | `loadSurface(id)` |
| PCM audio and format | `loadAudio(id)` |
| Text with encoding conversion | `loadTextUtf8(id, encoding)` |
| Raw decompressed bytes | `read(id)` |

Use `contains`, `list`, `openStream`, and `localPath` where appropriate. Always call **public** methods so overlays, caches, and original selection work. Text decoders understand UTF-8/UTF-16/UTF-32 BOMs; without a BOM, supply a charset such as `CP932` or `CP1252`. Binary scripts must use `read`.

| Module | Target | Format |
| --- | --- | --- |
| `cmp` | `Kotonoha::ProviderCMP` | Summer CMP |
| `adv` | `Kotonoha::ProviderADV` | Overflow DAT |
| `gpk` | `Kotonoha::ProviderGPK` | School Days GPK |
| `xp3` | `Kotonoha::ProviderXP3` | Kirikiri XP3 |
| `pak` | `Kotonoha::ProviderPAK` | LucaSystem PAK |
| `reallive` | `Kotonoha::ProviderRealLive` | Seen/RealLive |
| `rpa` | `Kotonoha::ProviderRPA` | DDLC RPA |
| `amagami` | `Kotonoha::ProviderAmagami` | Amagami |
| `pmail` | `Kotonoha::ProviderPmail` | Pure Mail |
| `zero` | `Kotonoha::ProviderZero` | Zero No Tsukaima |

Format-specific adapters may depend on their game library and cannot always be built in isolation. Generic filesystem and disc providers live in the shared engine. See [overlays](../../docs/ASSET_OVERLAYS.md).
