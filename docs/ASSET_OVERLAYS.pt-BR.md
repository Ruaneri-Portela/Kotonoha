# Overlays de assets e traduções

[English (US)](ASSET_OVERLAYS.md) | **Português (Brasil)**

A API compartilhada `AssetProvider` consulta a pasta opcional de substituições **antes** dos dados originais em leituras, decodificação, listagem, verificação e streaming. O overlay nunca altera a instalação.

```sh
Game --asset-overlay /caminho/para/alteracoes
Game --no-asset-overlay
```

O launcher também oferece **Game → Asset overlay**. Os caminhos devem corresponder aos identificadores lógicos, como `bgm/1.ogg` (DDLC), `script/START.KS` (Zero) ou `g00/name.g00` (RealLive). Nomes são normalizados quanto à caixa ASCII e separadores; duplicatas ambíguas, travessia de diretórios e links externos são rejeitados. Remonte o overlay após adicionar arquivos. Recursos já decodificados podem permanecer no cache até sua invalidação.

## Tradução e entrada

- O editor de traduções atualiza textos, escolhas e legendas em execução sem reiniciar a cena. Exporte com `--dump-translation ARQUIVO`: o jogo inicia, exporta e termina sem avançar a história.
- A tradução opcional VNX do DDLC pode ser aplicada como overlay, mantendo a instalação original separada. Scripts soltos e pacotes de patch têm prioridade.
- Ações comuns: Escape/Voltar Android/clique direito para voltar; B/Page Up/roda para cima para histórico. Ports podem redefinir ações para seus menus nativos.
- O cache gráfico evita processar a mesma textura repetidamente; glifos da interface e legendas mantêm resolução própria.

**Contrato para desenvolvedores:** use os métodos públicos de `AssetProvider` (`read`, `loadSurface`, `texture`, `loadAudio` etc.) em vez de ignorar os overlays por métodos específicos. `FileResolver` determina os caminhos por plataforma. Veja [provedores](../modules/providers/README.pt-BR.md), [diretórios](DIRECTORY_POLICY.pt-BR.md) e [configurações](ENGINE_SETTINGS.pt-BR.md).
