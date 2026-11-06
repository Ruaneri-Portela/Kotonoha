# Rastreamento do School Days HQ original

## Observador externo disponível

`tools/schooldays_parity/trace_original_session.py` observa uma instância já
aberta do jogo original. Registra em JSONL:

- identidade SHA-256 do executável e PID/janela;
- cliques dentro da janela e teclas de controle enquanto ela está em primeiro plano;
- hash de quadros visíveis alterados, com PNG local opcional;
- criação, alteração ou remoção de `Save/*.DAT`, com tamanho e hash.

O observador filtra os eventos pela janela do jogo em primeiro plano e registra
somente teclas de controle; texto digitado em comentários de save não é coletado.

Não altera o EXE, as DLLs nem a memória do processo. Esse registro representa
o que é observável de fora; **não revela** por si só SceneKey, flags internas,
funções chamadas ou quais entradas dos GPK foram lidas. Para esses pontos será
necessário rastreamento dirigido depois de identificar os endereços e o formato
dos dados com a pesquisa existente e o Ghidra.

## Uso

Abra o original ou uma cópia isolada. Em PowerShell, descubra o PID:

```powershell
Get-Process -Name 'SCHOOLDAYS HQ' | Select-Object Id,Path
```

Com o Python do ambiente `build/school-days-parity-venv`:

```powershell
& 'build/school-days-parity-venv/Scripts/python.exe' `
  'tools/schooldays_parity/trace_original_session.py' `
  --pid 12345 `
  --original 'D:\SCHOOL DAYS HQ\Overflow\SCHOOLDAYS HQ' `
  --output 'build/school-days-original-traces/minha-sessao' `
  --screenshots
```

Sem `--screenshots`, o log contém apenas hashes dos quadros. `--duration 60`
limita a captura a um minuto; sem duração ela termina quando a janela fecha ou
com Ctrl+C. A saída deve permanecer local: PNGs podem conter arte original.
Para comparar perfis limpo e concluído, execute o jogo em duas cópias isoladas
e passe a pasta correspondente em `--original`.

## Verificação em runtime

Em 04/11/2026, o observador foi executado contra a cópia isolada `D-empty`
do original v1.02. Uma sessão de 20 segundos registrou o SHA-256 do EXE,
quatro mudanças visíveis de quadro, um clique com posição normalizada
`(0.61, 0.79111)` e encerramento por duração. O jogo continuou funcional; o
processo de teste isolado foi encerrado depois. O primeiro ensaio detectou um
conflito no nome do campo `kind` do evento; a correção foi aplicada e a sessão
seguinte passou.

## Próxima profundidade de log

A primeira rota piloto com perfis limpos está documentada em
[CLEAN_PROFILE_PILOT.md](CLEAN_PROFILE_PILOT.md).

1. Correlacionar cliques, capturas e mudanças de Save com as mesmas ações na
   build Kotonoha, mantendo os perfis separados.
2. Para uma divergência concreta, localizar no Ghidra a função ou variável
   pertinente em `RouteProcSDHQ.dll` ou `SysMenuSDHQ.dll` e registrar apenas
   seus parâmetros/resultados no original. Um depurador ou hook dirigido pode
   fazer isso; Frida é uma opção, não um requisito da coleta externa.
3. Manter os logs em arquivos locais e publicar apenas conclusões e casos de
   teste derivados, sem frames, áudio, saves ou binários originais.

## Skills pesquisadas

O REA já cobre análise estática, comparação de evidências e investigação de
runtime. No GitHub, as skills mais pertinentes como referência complementar são
[Ghidra headless de mitsuhiko](https://github.com/mitsuhiko/agent-stuff),
[análise com Ghidra](https://github.com/meltedinhex/analyst-ai-pack/blob/main/skills/reverse-engineering-binaries-with-ghidra/SKILL.md),
[automação headless](https://github.com/meltedinhex/analyst-ai-pack/blob/main/skills/scripting-ghidra-headless-analysis/SKILL.md),
[análise de rastros de API](https://github.com/meltedinhex/analyst-ai-pack/blob/main/skills/analyzing-api-call-traces/SKILL.md) e
[monitoramento de processo/arquivos](https://github.com/meltedinhex/analyst-ai-pack/blob/main/skills/monitoring-process-and-file-behavior/SKILL.md).
Essas fontes foram pesquisadas como instruções, não instaladas nem executadas.
A coleção de 118 skills do Analyst AI Pack é majoritariamente voltada a
malware; importar tudo adicionaria procedimentos sem relação com a paridade
de School Days. O caminho mais útil é adaptar as poucas técnicas relevantes ao
laboratório já existente e aos testes de comportamento do jogo.
