# Kotonoha v3 — colaboração no VS Code

O alvo atual do School Days HQ v1.02 é **somente Windows L4**.
Código genérico em `lib/`; código específico em `games/school-days/`.

Prioridades: paridade retail, segurança dos dados, testes reproduzíveis e
diferenças explícitas entre launcher (`settings.json`) e Display/General
(`Config.DAT`/preferências do jogo).

## Limites inegociáveis

- Nunca rodar o EXE/instalador retail nem alterar os 29 GPKs originais;
  tampouco sobrescrever `LxH.GPK`.
- Não alterar saves reais nem configurações pessoais: para teste usar
  `--documents` e `--config` em `build/` isolado, com assets sintéticos sempre
  que possível.
- Não iniciar outra GUI se já houver `SchoolDays.exe` rodando.
- Não comitar ou fazer push, publicar Release, atualizar staging instalado,
  copiar GPKs ou criar PR sem solicitação explícita.
- Não incluir tradução PT-BR privada nem ativos comerciais em Git público.
- Não iniciar Android antes da conclusão do Windows.
- Não reintroduzir MCP/DevBridge game-specific: usar MCP genérico em `lib/`.

## Verificação

Compilar com `cmake --build build/school-days-l4-two-dll-staticmd --config
Release --parallel 1`. Para stress e CTest usar
`games/school-days/tools/stress_l4.py` ou as tarefas de `.vscode/tasks.json`.
Relatar regressões verificadas com arquivos/linhas e separar verificações
estáticas, unitárias, GPU real e QA visual manual. Nunca prometer encontrar
absolutamente todos os bugs ou executar trabalho em background.
