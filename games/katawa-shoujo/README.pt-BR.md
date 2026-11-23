# Katawa Shoujo

[English (US)](README.md) | **Português (Brasil)**

Katawa Shoujo compartilha o runtime Ren'Py e o provedor de arquivos RPA do Kotonoha. Alvo CMake independente: `KatawaShoujo`; identificador do pacote: `me.hirameki.kotonoha.katawashoujo`.

## Compilação e execução

Configure com `-DKOTONOHA_BUILD_KATAWA=ON` e compile `KatawaShoujo`. Use dados adquiridos legalmente por `--katawa DIRECTORY|ISO`, `KATAWA_DIR` ou pelo launcher. Os dados locais ficam em `games/katawa-shoujo/game-assets/`. A validação procura `game/script-a1-monday.rpyc` (ou o diretório `game` selecionado).

## Estado

O runtime Ren'Py é compartilhado com DDLC; o título e o identificador do pacote dependem dos dados detectados pelo provedor. A preparação do CPython varia por plataforma. Compilar e passar em um smoke test não comprova a compatibilidade integral com o jogo.
