# Integração com Ruaneri main — 04/11/2026

Base upstream: `Ruaneri-Portela/Kotonoha` `main` em `3dff544`. A integração foi
feita na branch `kotonoha-v3-ruaneri-sync-transitions`; `kotonoha-main` permanece
separada. A branch vazia foi enviada ao fork antes do commit, conforme pedido.

## Mudanças verificadas

- O merge incorpora as correções recentes do upstream para entrada por controle,
  legendas, mídia, caminhos, compilação e os demais jogos, mantendo a lógica
  específica de School Days v3 e as transições de menu rastreadas no original.
- School Days inicializa um `GlobalFlag` vazio e válido quando não existe arquivo
  persistido. Isso permite salvar na primeira execução, sem dados de usuário.
- O logo aceita a imagem de um overlay do jogo mesmo quando o arquivo base não
  está presente na instalação. O diálogo do menu de save alternativo recebe a
  entrada antes da lista de slots, inclusive Esc após a criação.
- A transição de cena só captura uma imagem anterior quando existe um quadro já
  apresentado; iniciar por salto de depuração não gera um erro falso no controle
  de desenvolvimento.
- Os testes de Save/Load usam a escala do CMAP original (800×450) para enviar
  cliques no espaço lógico de 1280×720 e esperam a animação de 750 ms antes de
  interagir. O teste de mídia espera a preparação assíncrona do vídeo.
- Foram ajustadas a conversão do alias `LATIN1`, a cópia RGB com opacidade, o
  fechamento do histórico pelo controle e a localização de DLLs de testes no
  Windows. O teste macOS de bundle evita criar um link simbólico no Windows.

## Evidência em runtime

- `cmake --build build/school-days-dev-control2 --config Release -j 8`: compilou.
- `ctest --test-dir build/school-days-dev-control2 -C Release --output-on-failure
  --timeout 30`: **55/55 testes passaram**; inclui Save com a instalação
  original, replay, mídia, transições, launcher e testes do motor.
- `cmake --build build/school-days-dev-control4 --config Release --target SchoolDays
  -j 8`: compilou o executável de desenvolvimento atualizado.
- O executável `SchoolDays.exe` da build control4 iniciou cinco quadros do nó 0
  com `D:\SCHOOL DAYS HQ\Overflow\SCHOOLDAYS HQ` e terminou com código 0,
  sem o erro de quadro de saída ausente.

Esses testes verificam os fluxos citados; não constituem uma comparação visual
exaustiva de todos os episódios nem uma certificação de paridade de 100%. Os
assets originais foram usados localmente na validação e não entram no repositório.
