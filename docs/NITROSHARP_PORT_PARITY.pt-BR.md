# Compatibilidade NitroSharp no Kotonoha/N2System

Este arquivo de estado é intencionalmente conservador: um teste sintético
aprovado **não** equivale a executar corretamente todos os cenários
comerciais de NSB.

Referência: [CommitteeOfZero/nitrosharp](https://github.com/CommitteeOfZero/nitrosharp),
branch `rebuild`. Evidência adicional de opcodes:
[FGRE/libnpa](https://github.com/FGRE/libnpa), `include/nsbmagic.hpp`.

## Estado por subsistema

| Área | Implementado no Kotonoha | Ainda necessário para paridade 1:1 completa |
| --- | --- | --- |
| Agendamento de processos/threads | Armazenamento estável em deque, orçamentos cooperativos, início de processo com atraso de frame, destruição diferida de processos, ações nativas de Request, E/S de save diferida | Ambientes de execução separados por processo, ciclo de vida exato de World/Thread/Process do NitroSharp, escopos de alias entre processos e temporização |
| Despacho de funções builtin | NSB Time, DateTime, ExistSave, duração/restante/decorrido de mídia, Save/Load/DeleteSaveFile, opcodes existentes de animação e diálogo/áudio | Cobertura completa de opcodes, assinaturas exatas de operandos para cada versão do jogo, semântica completa de erro/resultado, recursos específicos de plataforma |
| Animações | Todas as onze curvas de easing do NitroSharp, movimento por Bezier cúbico, movimento/fade/rotate/zoom/transition com easing, esperas de animação da VM | Transformações 3-D compostas, efeitos de câmera, convenções exatas de parâmetros de curva, transições mascaradas, easing em todas as entidades criadas em runtime |
| Gráficos | Monocromático RGBA em cache e blur premultiplicado de 8/16 passos, `SetTone` (0x02e), `SetShade` (0x02d), restauração de estado de cor/alfa | Paridade de shader GPU com NitroSharp, gradientes/lente/contorno/cubo 3D, grafo completo de máscara/efeito e comparações de imagem nas GPUs-alvo |
| Texto | Atlas de texto do renderizador `Kotonoha::UI` com bytes da fonte nativa do jogo, sem criação de surface de texto por frame | Parser de markup completo do NitroSharp, pesos de fonte, kerning, quebras de linha japonesas, texto ruby, todas as opções de estilo/contorno específicas de cada motor |
| Persistência | Extensão binária N2SV (N2EX/N2RS) armazena chamadas NSB, variáveis, processos, cena/animação, telefone, roteamento de áudio, diálogo/backlog; roundtrip estrito de save JSON de debug; verificações de corrupção; E/S de arquivo diferida | Retomada exata de timestamps de áudio/vídeo, compatibilidade completa com o formato de slot original do NitroSharp, migrações de schema entre versões e validação de jogo commercial de longa duração |

## Cobertura de regressão

- `Kotonoha.N2System.NsbVm` (CMake / CTest) valida bytecode NSB,
  callbacks nativos de builtin, agendamento de threads, criação aninhada de
  processos, save/load/delete diferidos, extensão e roundtrip JSON,
  rejeição de save truncado, persistência de estado gráfico e retomada de
  frame.
- `Kotonoha.N2System.EntityQuery` verifica a gramática de consulta/caminho
  derivada da fonte, aliases, pais, ordem de exclusão e inserção.
- `Kotonoha.N2System.AnimationTiming` verifica todas as curvas de easing,
  geometria de bezier e conclusão de tweens.
- `Kotonoha.N2System.ImageEffects` verifica alfa premultiplicado e seleção
  de efeitos.
- `.github/workflows/port-contracts.yml` compila essas fontes em C++17;
  `.github/workflows/linux-system-deps.yml` compila com verificação de
  sintaxe o runtime de apresentação do N2System contra SDL3.

## Validação necessária antes de afirmar paridade completa

1. Disponibilize **legalmente** os dados do jogo localmente via
   `KOTONOHA_STEINSGATE_DIR`; nunca envie arquivos comerciais ao GitHub.
2. Execute o N2System do boot até o título, escolhas, todas as rotas e
   finais principais, incluindo scripts com processos executando em
   concorrência.
3. Compare frames de referência de overlays, fontes, transições de vídeo,
   máscaras, layout de texto e saída de shader em Linux/Windows/macOS e
   mobile/web.
4. Verifique save/reload enquanto o diálogo está sendo revelado, durante
   animações, durante reprodução de áudio/vídeo e com múltiplas threads de
   background.
5. Confirme compatibilidade com saves existentes e exercite corrupção do
   parser, pouca memória, assets ausentes e falhas de operações de
   armazenamento.

O PR [#76](https://github.com/Ruaneri-Portela/Kotonoha/pull/76) foi mesclado
em `dev` em 2026-11-22 como **implementação incremental**, não como
afirmação de compatibilidade 1:1 completa. O PR
[#71](https://github.com/Ruaneri-Portela/Kotonoha/pull/71) integrou
posteriormente entrada de vídeo/menu e regressões de composição do
LucaSystem.

Continue acompanhando os itens de equivalência pendentes e os testes de
varejo/plataforma na
[issue #83](https://github.com/Ruaneri-Portela/Kotonoha/issues/83). Essa
issue deve permanecer aberta até que esses critérios tenham evidência
reprodutível.
