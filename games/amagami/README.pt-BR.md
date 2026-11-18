# Amagami

[English (US)](README.md) | **Português (Brasil)**

Runtime SCF e apresentação/mídia específicas. Prólogo e mapa inicial de eventos jogáveis.

**Estado:** Calendário, conversas, rotas e finais completos ainda não foram validados.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_AMAGAMI=ON
cmake --build build --target Amagami
```

## Dados do jogo

Forneça disco/imagem de PS2 ou dados extraídos do Amagami, obtidos legalmente e aceitos pelo launcher.
Use o launcher ou passe `--amagami /caminho/para/dados` ao executável. Nunca versione dados comerciais.

## Investigação detalhada

[Resumo do estado do port](STATUS.pt-BR.md) · [Investigação detalhada](PORT_STATUS.md). Resultados históricos se aplicam aos assets/revisão analisados e podem conter informações substituídas depois.

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)

## Amagami no navegador (WASM)

O navegador pode abrir uma imagem original de PS2 por **HTTP Range**, sem
baixar o BIN inteiro para a memória. Escolha **Usar HTTP(S)** na tela inicial.
A imagem deve estar no endereço `game-assets/Amagami.bin` (ou ser uma das
alternativas que o launcher reconhece: `Amagami.iso` e `Amagami (Japan).iso`).
O `manifest.json` produzido na compilação pode conter
`{"files":[],"entries":[]}`: a escolha HTTP(S) utiliza descoberta direta
por HEAD, portanto não é necessário embutir o disco no pacote nem regenerar
o manifesto para um arquivo montado no servidor depois.

No WASM, o arquivo `SCENARIO.ARC` é consultado em entradas individuais, em
vez de criar uma cópia residente do arquivo completo. Isso reduz o pico de
memória, mas a indexação inicial dos cenários ainda pode exigir muitas
requisições de rede; não representa suporte integral ao jogo.

Para conferir Nginx, permissões e `Range` antes de abrir o navegador:

```sh
python3 tools/check_wasm_disc.py \
  "http://localhost/games/wasm/Amagami/game-assets/Amagami.bin" \
  --local /var/www/games/wasm/Amagami/game-assets/Amagami.bin
```

No Debian, se o Nginx é executado como `www-data`, faça também:

```sh
sudo -u www-data test -r /var/www/games/wasm/Amagami/game-assets/Amagami.bin
namei -l /var/www/games/wasm/Amagami/game-assets/Amagami.bin
```

Um link para `/mnt/data2/Games/Amagami.bin` só funciona se o destino for um
**arquivo real** e os diretórios puderem ser atravessados pelo usuário do
servidor. Um segundo link `Amagami.bin -> Amagami.bin` cria um loop e impede
a abertura do arquivo. Corrija o destino, sem liberar permissões globais
desnecessárias. A resposta HTTP precisa incluir `Accept-Ranges: bytes` no
HEAD e `206 Partial Content` no GET com `Range`.

Testes do verificador, sem dados comerciais:

```sh
python3 -m unittest discover -s tools -p 'test_check_wasm_disc.py'
```
