# Configurações do motor

[English (US)](ENGINE_SETTINGS.md) | **Português (Brasil)**

O menu interno oferece **Display**, **Audio** e **VN text**. O launcher tem opções adicionais em **Advanced**. Preferências ficam salvas por jogo e arquivos antigos continuam aceitos.

- **Display:** VSync é ligado por padrão, salvo quando desativado explicitamente. Upscaling pode ser ajustado por backend/perfil/escala. Shaders disponíveis dependem do renderizador ativo. Texto e legendas são rasterizados separadamente, sem passar pelo upscale de imagens.
- **Audio:** volume/mute geral e canais do mixer fornecidos pelo port. Mudanças são imediatas e preservam fades/ganhos nativos do roteiro.
- **VN text:** seleção opcional de fonte (`.ttf`, `.otf`, `.ttc`) e cores independentes para diálogo e nome. Mudanças são aplicadas sem reiniciar a visual novel.

## Legendas ASS forçadas

A opção **Enable forced subtitles** vem ativada e é salva como `enableForcedSubtitles`. A pasta `forced_subtitles/` pode fornecer arquivos `.ass` pelo ID do nodo/evento (exemplo: `80005:66` vira `80005-66.ass`). Cenas Timed usam o relógio da timeline; cenas Standard contam tempo de cena não pausado. F8 mostra IDs e tempo no HUD.

No inspetor F10, **Generate Forced Subtitles (.ass)** cria um modelo para o nodo sem sobrescrever arquivos. Edite o ASS, altere `Comment` para `Dialogue`, ajuste tempo/texto e use **Reload forced subtitles**. O inspetor mostra a pasta resolvida e o arquivo ativo. Menus pausam o relógio compartilhado enquanto a legenda permanece atrás da interface.

## Outros guias

[Diretórios](DIRECTORY_POLICY.pt-BR.md) · [Overlays e tradução](ASSET_OVERLAYS.pt-BR.md). Algumas opções variam entre os ports.
