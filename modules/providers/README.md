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
| `cmp` | `kotonoha_provider_cmp` | Summer CMP |
| `adv` | `kotonoha_provider_adv` | Overflow DAT |
| `gpk` | `kotonoha_provider_gpk` | School Days GPK |
| `xp3` | `kotonoha_provider_xp3` | Kirikiri XP3 |
| `pak` | `kotonoha_provider_pak` | LucaSystem PAK |
| `reallive` | `kotonoha_provider_reallive` | Seen/RealLive |
| `rpa` | `kotonoha_provider_rpa` | DDLC RPA |
| `amagami` | `kotonoha_provider_amagami` | Amagami |
| `pmail` | `kotonoha_provider_pmail` | Pure Mail |
| `zero` | `kotonoha_provider_zero` | Zero No Tsukaima |
| `n2` | `kotonoha_provider_n2` | NPA/AFS archives |

Format-specific adapters may depend on their game library and cannot always be built in isolation. Generic filesystem and disc providers live in the shared engine. See [overlays](../../docs/ASSET_OVERLAYS.md).
