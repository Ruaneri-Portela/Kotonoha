# Pure Mail

[English (US)](README.md) | **Português (Brasil)**

Port nativo do DVD com progressão PRO1, decodificação de animações, áudio e saves.

**Estado:** Opcodes da interface do computador, transições e finais completos ainda precisam de validação.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_PMAIL=ON
cmake --build build --target Pmail
```

## Dados do jogo

Use a pasta original PMAILDVD, com data/pack/*.dat e data/movie.
Use o launcher ou passe `--pmail /caminho/para/dados` ao executável. Nunca versione dados comerciais.

## Investigação detalhada

[Resumo do estado do port](STATUS.pt-BR.md) · [Investigação detalhada](PORT_STATUS.md). Resultados históricos se aplicam aos assets/revisão analisados e podem conter informações substituídas depois.

[Pesquisa de animações](docs/ANIMATIONS.pt-BR.md), [áudio](docs/AUDIO_PARITY.pt-BR.md) e [caixa de diálogo](docs/TEXTBOX.pt-BR.md).

[Guias de compilação](../../docs/BUILD_GUIDES.pt-BR.md) · [Índice de ports](../README.pt-BR.md)

## Dados do jogo no WebAssembly

No modo **Usar HTTP(S)**, a estrutura publicada deve incluir os arquivos
originais do jogo, por exemplo:

```text
Pmail/
  game-assets/
    data/
      pack/
        script.dat
        graphic.dat
        sound.dat
        voice.dat
```

O navegador deve conseguir abrir
`http://localhost:3000/Pmail/game-assets/data/pack/script.dat`
(com o host e a porta da sua instalação). O Kotonoha **não distribui**
esses arquivos. Um `manifest.json` vazio **não fornece** o jogo; neste caso,
o HTML usa Fetch direto pelos nomes conhecidos. O backend agora tenta uma
segunda consulta em minúsculas quando um nome lógico misto/maiúsculo recebe
404. O script e o índice interno do pacote são lidos da VFS, sem depender
de um caminho físico.

Mensagens `HEAD data` e `HEAD data/pack` sem `Content-Length` podem
aparecer em servidores que oferecem páginas de diretório; isso não significa
que `data/pack/script.dat` esteja disponível. Confirme o endereço do
arquivo antes de investigar o resto do gameplay.
