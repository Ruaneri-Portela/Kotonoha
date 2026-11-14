# Política de diretórios e recursos

[English (US)](DIRECTORY_POLICY.md) | **Português (Brasil)**

`FileResolver` separa **assets do motor**, **assets do jogo** e **saves/configurações graváveis**. Jogos não devem criar caminhos de armazenamento por conta própria. `<app-id>` é o identificador do pacote/bundle, não a pasta do jogo.

| Plataforma | Assets do motor | Assets do jogo | Saves/configurações |
| --- | --- | --- | --- |
| Android | `<dataDir>/files/assets` | `<dataDir>/game_assets`; OBB externo permitido | `Android/data/<app-id>/files/Kotonoha/<app-id>` |
| iOS | `<bundle>/assets` | `<bundle>/game-assets`; alternativa Documents | `Documents/Kotonoha/<app-id>` |
| macOS | `<app>.app/Contents/Resources/assets` | `.../Resources/game-assets` | `~/Library/Application Support/Kotonoha/<app-id>` |
| Windows | `<exe>/assets` | `<exe>/game-assets` | `%APPDATA%/Kotonoha/<app-id>` |
| Linux | `<exe>/assets` | `<exe>/game-assets` | `$XDG_DATA_HOME/Kotonoha/<app-id>` |

No desktop fora de bundles, o diretório de trabalho também é procurado. `game-assets` e `game_assets` são aceitos para encontrar recursos. OBB externo pode ter prioridade no Android; iOS pode consultar Documents.

## Fontes de dados, ISOs e patches

O launcher consulta locais conhecidos da plataforma, sem vasculhar todo o disco. ISO explicitamente informada e inválida gera erro. RealLive pode ler ISO diretamente; outros ports de pasta podem extrair temporariamente para armazenamento privado. Pastas comuns são lidas no lugar.

`--asset-overlay CAMINHO` substitui recursos pelo **ID lógico**, independentemente de pasta/arquivo/ISO original. Caminhos explícitos têm prioridade; `--no-asset-overlay` desativa patches. As preferências normalmente salvam nomes portáteis que são resolvidos novamente ao iniciar; caminhos absolutos antigos ainda podem ser tentados.

## Detalhes de runtime

O catálogo de fontes aceita `.ttf`, `.otf` e `.ttc`. Saves são separados por identificador do aplicativo; saves antigos por slug podem exigir migração manual. Veja [overlays](ASSET_OVERLAYS.pt-BR.md) e [configurações](ENGINE_SETTINGS.pt-BR.md).
