# Provedores de assets

[English (US)](README.md) | **Português (Brasil)**

Os provedores implementam `Kotonoha::AssetProvider` e oferecem os mesmos métodos de acesso independentemente do formato.

| Operação | Método público |
| --- | --- |
| Textura em cache | `texture(renderer, id)` |
| Imagem decodificada | `loadSurface(id)` |
| Áudio PCM e formato | `loadAudio(id)` |
| Texto com conversão | `loadTextUtf8(id, encoding)` |
| Bytes brutos descompactados | `read(id)` |

Use `contains`, `list`, `openStream` e `localPath` quando necessário. Sempre invoque os métodos **públicos** para respeitar overlays, caches e seleção original. Textos reconhecem BOMs UTF-8/UTF-16/UTF-32; sem BOM, informe `CP932`, `CP1252` etc. Scripts binários devem usar `read`.

| Módulo | Alvo | Formato |
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

Adaptadores específicos podem depender do jogo e não funcionar isoladamente. Provedores genéricos de filesystem/disco fazem parte do motor. Veja [overlays](../../docs/ASSET_OVERLAYS.pt-BR.md).
