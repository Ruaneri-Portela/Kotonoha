# Pure Mail

[English (US)](README.md) | **Português (Brasil)**

Port nativo do DVD com progressão PRO1, decodificação de animações, áudio e saves.

**Estado:** Texto posicionado, relógio e histórico do chat do computador já têm estado e renderização, além de testes sintéticos. A fidelidade visual das cores e posições, os comandos de e-mail/ícones, as transições e os finais completos ainda precisam de validação.

## Compilação

```sh
cmake -S . -B build -DKOTONOHA_BUILD_PMAIL=ON
cmake --build build --target Pmail
```

## Dados do jogo

Use a pasta original PMAILDVD, com data/pack/*.dat e data/movie.
Use o launcher ou passe `--pmail /caminho/para/dados` ao executável. Nunca versione dados comerciais.

## Personalização, saves e upscaling

Selecione **Novo jogo** para editar os nomes que substituem `$NO` e `$HO`
nos diálogos. Os valores são gravados no perfil `pmail-names.txt` e nos
saves. A janela de nomear/renomear saves agora recebe teclado e aparece sobre
a lista de slots. Saves antigos continuam aceitos pelo porte.

Os parâmetros originais `MesWin_R`, `MesWin_G` e `MesWin_B` do
`OF_ADV2.INI` controlam a tonalidade do fundo da caixa de diálogo. É
possível testar ajustes temporários por `PMAIL_MESWIN_R/G/B`.

As imagens e o título usam o upscaler compartilhado do Kotonoha. Para
ativá-lo, configure fator maior que 1 no menu gráfico; o backend depende
do renderizador SDL (Linear é a alternativa compatível).

A animação antes da imagem Overflow ainda exige comparação com o DVD original
e seus assets: o conteúdo e os tempos exatos não estão comprovados.

**Correção na progressão:** quando um script secundário termina, a VM
retoma o script anterior, em vez de permanecer marcada como encerrada.
O teste sintético `Pmail.ComputerSave` cobre esse retorno, o `SYS 213`
do título e um save/load durante a execução do script secundário.
Os quadros da animação de boca agora respeitam os limites do recurso.
A jogabilidade completa ainda depende de validação com os dados do DVD.

**Acesso rápido:** F5 salva em `pmail-quick.vn` dentro do diretório de saves
do jogo e F9 carrega esse estado. Os slots normais continuam disponíveis.
Essas teclas são conveniências do Kotonoha, não atalhos confirmados do original.

## Auditoria de rotas e jogabilidade

Com os dados originais do seu jogo, compile o auditor de rotas. Ele avança
diálogos e escolhas, registra os scripts percorridos e identifica comandos
nativos ou instruções ainda sem suporte. Você pode passar uma sequência de
índices de escolha, por exemplo `0,1,2`, e um limite de tempo em segundos:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DKOTONOHA_BUILD_PMAIL=ON \
  -DPMAIL_TEST_ASSET_DIR=/caminho/PMAILDVD
cmake --build build --target PmailRouteAuditCheck
ctest --test-dir build -R 'Pmail.RouteAudit' --output-on-failure
# Ou chame PmailRouteAuditCheck com: /caminho/PMAILDVD 0,1,2 300
```

A CI só executa testes sintéticos, incluindo `Pmail.Expression32`,
`Pmail.ComputerSave` e `Pmail.TransitionTrace`. A
auditoria real reprova quando encontra scripts ausentes, bytecodes
desconhecidos ou SYS ainda não implementados. Não significa que todos os
finais já estejam reproduzidos.

## Gate retail do Large PonPon

`Pmail.LargePonPon.Retail` tem o rótulo `real` e é sempre registrado, mas
retorna 77 ("Skipped" no CTest) até que um diretório retail seja informado em
`LARGEPONPON_TEST_DATA`. O teste monta `PACK/script.dat`, `PACK/GRAPHIC.DAT` e
`PACK/SOUND.DAT`, decodifica gráficos e áudio reais e inicializa o runtime em
modo headless da título até o diálogo de abertura. O diretório do jogo é apenas
lido.

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DKOTONOHA_BUILD_PMAIL=ON \
  -DLARGEPONPON_TEST_DATA=/path/to/LargePonPon
cmake --build build --target LargePonPonCheck
ctest --test-dir build -R '^Pmail\.LargePonPon' --output-on-failure
```

Sem o gate, o mesmo comando CTest executa o teste sintético
`Pmail.LargePonPon` e reporta o teste retail como ignorado.


## Investigação detalhada

[Resumo do estado do port](STATUS.pt-BR.md) · [Investigação detalhada](PORT_STATUS.md). Resultados históricos se aplicam aos assets/revisão analisados e podem conter informações substituídas depois.

[Inicialização](docs/STARTUP.pt-BR.md), [cobertura dos SYS nativos](docs/NATIVE_COVERAGE.pt-BR.md), [rastreamento de transições](docs/TRANSITIONS.pt-BR.md), [pesquisa de animações](docs/ANIMATIONS.pt-BR.md), [áudio](docs/AUDIO_PARITY.pt-BR.md) e [caixa de diálogo](docs/TEXTBOX.pt-BR.md).

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
