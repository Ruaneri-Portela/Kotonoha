# Laboratório QA isolado — Kotonoha v3 (Windows L4)

Este laboratório fica **inteiramente em `tools/qa-lab/`**. Foi criado para
orquestrar testes do School Days HQ de forma rápida, econômica em chamadas
MCP, reproduzível e **sem Codex**. O ChatGPT desta conversa utiliza o conector
Chat On Steroids para executar os mesmos comandos que aparecem nas tarefas do
VS Code; outros clientes MCP compatíveis podem usar o servidor stdio local.

> **Escopo atual, v0.1:** testes sintéticos de inicialização, renderer,
> upscale/backend, preset, fator, escala e persistência de configurações,
> além de verificação do transporte MCP. Isto **não** certifica cenas retail,
> escolhas naturais, playback de mídia, áudio ouvido, fidelidade visual de
> shaders, launcher gráfico nem a conclusão dos sete finais L×H.

## Estrutura

```text
tools/qa-lab/
  lab_core.py       gerador de matriz, isolamento, execução, análise
  lab_cli.py        orquestração pelo terminal e por Chat On Steroids
  lab_mcp.py        MCP stdio com 5 ferramentas de controle
  test_lab.py       testes unitários do laboratório e handshake MCP real
  README.md         este guia
  state/            saídas locais ignoradas pelo Git
    runs/run-*/     caso, documents/, settings.json, logs, report.json
    staged/         reservado para instalações futuras
```

Todos os relatórios, settings, saves de fixture, cópias dos binários, DLLs,
assets e logs gerados **ficam em `tools/qa-lab/state/`**. Cada rodada recebe
uma pasta exclusiva, e o EXE/DLLs/assets são copiados em
`state/runs/run-*/staged-runtime/`, sem executar a instalação comercial. O
binário de entrada é apenas **lido** de
`build/school-days-l4-two-dll-staticmd/games/school-days/Release/`, e usa
as fixtures sintéticas daquela build, mais o KTRF de referência do repositório.

O `--documents`, `--config`, `--log` e diretório de trabalho apontam para a
rodada isolada. O laboratório **não** altera o código da engine, os 29 GPKs
originais, o LxH.GPK, a instalação L4, os saves reais ou o Git.

**Limite técnico importante:** isolamento por diretórios e argumentos não é
uma sandbox de segurança do Windows. Como todo processo nativo, o próprio
`SchoolDays.exe` possui as permissões do usuário. Por isso o laboratório
executa somente a build conhecida, com assets sintéticos e sem executar
instalador ou executável retail. Não forneça arquivos não confiáveis.

## Executar pelo terminal ou pelo ChatGPT via Chat On Steroids

Na raiz `D:\Dev\Kotonoha-v3`:

```powershell
python tools/qa-lab/lab_cli.py check
python -m unittest discover -s tools/qa-lab -p test_*.py -v

# Execução rápida: 4 processos sintéticos, até dois em paralelo
python tools/qa-lab/lab_cli.py run --mode smoke --count 4 --parallel 2 --wait

# Matriz maior, ainda sem janela nem GPU real
python tools/qa-lab/lab_cli.py run --mode matrix --count 32 --parallel 2 --wait

# Somente quando nenhum SchoolDays.exe estiver aberto: GPU real, serial
python tools/qa-lab/lab_cli.py run --mode gpu --count 12 --parallel 1 --wait

# Para disparar e receber apenas um ID (sem --wait):
python tools/qa-lab/lab_cli.py run --mode smoke --count 8
python tools/qa-lab/lab_cli.py status --run-id run-xxxxxxxxxxxx
python tools/qa-lab/lab_cli.py report --run-id run-xxxxxxxxxxxx
python tools/qa-lab/lab_cli.py list

# Verificação ponta a ponta: MCP stdio -> lab_start -> processo -> lab_status
python tools/qa-lab/mcp_e2e.py
```

`--seed` identifica a amostragem determinística; `--count` aceita 1 a 256,
`--parallel` aceita 1 a 4 (GPU exige 1) e `--timeout` de 5 a 120 segundos por
processo. O relatório registra os parâmetros, erros, sinais relevantes no log
e as **normalizações esperadas** (por exemplo MPV incompatível virando Auto)
separadamente das divergências inesperadas. Resultados completos são
escritos em `state/runs/run-*/report.json`; não são enviados integralmente ao
chat a cada etapa.

## VS Code: conexão e tarefas

O workspace `D:\Dev\Kotonoha-v3` possui duas configurações locais:

- `.vscode/tasks.json`: tarefas **QA Lab:** Preflight, Unit tests, Smoke 4,
  Settings matrix 32 e Renderer matrix 12 (GPU real). Acesse pelo menu
  **Terminal → Run Task…**; não é necessário consumir nenhuma sessão Codex.
  `QA Lab: Smoke 4` é a tarefa de teste padrão. As três tarefas de execução
  abrem/revelam seu próprio terminal e mostram o **progresso caso a caso**
  (`[1/4]`, `[2/4]`, ...), normalizações esperadas, falhas e caminho do
  relatório no final.
- `.vscode/mcp.json`: servidor **`kotonoha-qa-lab`** do tipo stdio iniciando
  `python ${workspaceFolder}/tools/qa-lab/lab_mcp.py`. A extensão/cliente MCP
  do IDE pode acessar `lab_preflight`, `lab_start`, `lab_status`, `lab_runs`
  e `lab_failures`. Ele **não** expõe terminal arbitrário, acesso à rede,
  edição de código ou caminhos de arquivo livres.

No VS Code compatível com MCP, use **MCP: List Servers** na paleta de comandos
para verificar/iniciar `kotonoha-qa-lab`; o cliente pode solicitar aprovação
para confiar no servidor. Se não houver um cliente MCP habilitado, as tarefas
continuam funcionando normalmente no terminal. A conexão MCP do VS Code
serve ao cliente de IA *dentro do editor*; **esta conversa no navegador não é
automaticamente a mesma sessão**. O ChatGPT aqui já acessa o workspace por
Chat On Steroids e pode orquestrar o laboratório com `lab_cli.py`.

A configuração MCP anterior (`kotonoha-l4-isolated`, que escrevia em `build/`)
foi retirada da lista de servidores automáticos do IDE para manter o único
servidor ativo restrito à área do laboratório. O arquivo Python original não
foi apagado. Backups dos JSONs de IDE anteriores estão em
`state/ide-backup-20261109/` para reversão se necessário. A pasta `.vscode/`
já é ignorada pelo `.gitignore` do repositório e não é enviada ao GitHub.

**Para acompanhar de verdade dentro do VS Code:** escolha o comando
`Terminal → Run Task… → QA Lab: Smoke 4 (headless, isolated)` ou a tarefa
`QA Lab: Settings matrix 32`. Uma execução disparada por ChatGPT via terminal
remoto/MCP fora do editor **não aparece automaticamente na lista de tarefas
nem no terminal integrado**: a pasta de relatório é compartilhada, mas as
sessões do terminal são independentes. Interromper apenas o visualizador
`--wait` não encerra necessariamente o worker local já iniciado; consulte
`lab_cli.py status --run-id ...` para acompanhar seu término.

## Uso responsável

- Não execute o perfil `gpu` junto com uma sessão de jogo; o laboratório
  verifica se existe `SchoolDays.exe` ativo antes de iniciar.
- `--node 0` de fixture é **smoke sintético** e não pode ser contado como uma
  rota natural até um final do HQ.
- A matriz prova apenas os comportamentos **observáveis**: código de saída,
  renderer identificado, fallback registrado e valores de configuração
  persistidos. Renderizadores, drivers e janelas podem falhar por limitações
  do equipamento, não necessariamente por defeito do jogo.
- Para ampliar cobertura (launcher gráfico, Save/Load, escolhas naturais,
  screenshots e comparação de pixels, métricas de áudio), acrescentar novos
  verificadores com fixtures controladas e resultados estruturados, **sem
  mover o código de produção para cá**.
- Não apagar indiscriminadamente `build/`: contém GPKs e junctions para a
  instalação comercial. Limpar rodadas antigas do laboratório somente após
  arquivar os relatórios desejados e verificar os destinos.

Nenhum commit ou push é feito pelo laboratório.
