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

Adaptadores específicos podem depender do jogo e não funcionar isoladamente. Provedores genéricos de filesystem/disco fazem parte do motor. Veja [overlays](../../docs/ASSET_OVERLAYS.pt-BR.md).
