# Pure Mail DVD: rastreamento de transições

[English (US)](TRANSITIONS.md) | **Português (Brasil)**

## O que está comprovado

A pesquisa do executável original `pmail.exe`, documentada em
[ANIMATIONS.md](ANIMATIONS.md), identificou `SYS 51` em `0x4240e0`.
No trecho `PRO1`, o preset 0 carrega `osboot.anim` na posição
**(245, 152)**, com **80 quadros a 54 ms** (aproximadamente **4320 ms**).
A animação exibe **FORIS OS 7.7** e bloqueia a VM até terminar.

**Ainda não foi demonstrado** que `osboot.anim` seja exatamente a
animação imediatamente anterior à imagem Overflow mencionada na captura.
A ligação entre esses eventos exige o rastreamento de uma rota com os
arquivos originais, sem presumir um novo efeito gráfico.

## Gerar o relatório localmente

Com os assets legítimos do DVD:

```sh
cmake -S . -B build -DBUILD_TESTING=ON \
  -DKOTONOHA_BUILD_PMAIL=ON -DPMAIL_TEST_ASSET_DIR=/caminho/PMAILDVD
cmake --build build --target PmailRouteAuditCheck
ctest --test-dir build -R '^Pmail.RouteAudit$' --output-on-failure
```

Também é possível executar o binário `PmailRouteAuditCheck` diretamente,
indicando a ordem de escolhas e o limite de tempo:

```sh
PmailRouteAuditCheck /caminho/PMAILDVD 0,1,2 300 > transicoes-pmail.tsv
```

O relatório fornece o tempo relativo (`ms`), script, posição da VM,
comando (`SYS 48/49/51/69`), nome do recurso, quadro amostrado e
coordenadas. Ele preserva os primeiros 200 e últimos 200 eventos, além de
uma janela anterior ao primeiro nome de recurso que contém `overflow`.

Se o logo tiver outro nome interno, aparecerá
`PMAIL_OVERFLOW_MARKER not-found`. Isso **não** significa que o logo
não foi exibido: é necessário inspecionar os demais recursos e transições.

A amostragem registra o quadro inicial, cada décimo quadro e o quadro final,
sem gerar logs de textos narrativos. Os tempos são do auditor, não de uma
execução capturada do programa Windows original.

## Comparação para aproximar 1:1

Execute a mesma rota no original e no Kotonoha e confronte a ordem dos
recursos, o preset das animações, as posições na tela original 800×600,
os quadros e o tempo entre cenas. Não modifique a animação anterior ao
logo por aproximação visual sem encontrar o recurso correspondente.

O teste sintético `Pmail.TransitionTrace` valida o formato e os limites
do relatório. `Pmail.ComputerSave` cobre a restauração de estado do
computador e de quadros. Os testes reais continuam separados e as Actions
do GitHub permanecem exclusivamente manuais.
