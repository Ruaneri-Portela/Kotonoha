# Directory and resource policy

**English (US)** | [Português (Brasil)](DIRECTORY_POLICY.pt-BR.md)

`FileResolver` separates **engine assets**, **game assets**, and **writable saves/settings**. Games should not build storage paths themselves. `<app-id>` is the package/bundle identifier, not a game folder slug.

| Platform | Engine assets | Game assets | Saves/settings |
| --- | --- | --- | --- |
| Android | `<dataDir>/files/assets` | `<dataDir>/game_assets`; external OBB supported | `Android/data/<app-id>/files/Kotonoha/<app-id>` |
| iOS | `<bundle>/assets` | `<bundle>/game-assets`; Documents fallback | `Documents/Kotonoha/<app-id>` |
| macOS | `<app>.app/Contents/Resources/assets` | `.../Resources/game-assets` | `~/Library/Application Support/Kotonoha/<app-id>` |
| Windows | `<exe>/assets` | `<exe>/game-assets` | `%APPDATA%/Kotonoha/<app-id>` |
| Linux | `<exe>/assets` | `<exe>/game-assets` | `$XDG_DATA_HOME/Kotonoha/<app-id>` |

On desktop outside an app bundle, the working directory is also searched. Both `game-assets` and `game_assets` are accepted for resource lookup. Android OBB resources may take priority over packaged assets; iOS can fall back to its Documents directory.

## Game sources, ISO images, and patches

The launcher searches known platform-specific locations instead of scanning the entire disk. An explicit invalid ISO reports an error. RealLive can stream directly from ISO; other directory-oriented ports may extract an ISO into temporary private storage. Ordinary folders are read in place.

`--asset-overlay PATH` replaces resources by **logical ID**, independent of their original folder/archive/ISO source. Explicit paths take priority; `--no-asset-overlay` disables overlays. Resource choices are generally saved by portable names and resolved again on launch; legacy absolute paths can still be tried.

## Runtime details

Font selection accepts `.ttf`, `.otf`, and `.ttc`. Saves are per application ID; existing saves in older slug-based locations may need manual migration. See [overlays](ASSET_OVERLAYS.md) and [settings](ENGINE_SETTINGS.md).
