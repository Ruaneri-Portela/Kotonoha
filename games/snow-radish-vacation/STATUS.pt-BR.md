# Snow Radish Vacation!! — Status do Port

## Engine
Overflow ADV 3.05 (`of_adv3.exe`, MSVC 6.0, `800×600`)

## Status: Jogável ✅

### Auditoria REA (2026-11-18)
- Engenharia reversa 1:1 clean-room completa de `of_adv3.exe` (499.712 bytes)
- 46 nativos sys auditados em 33 scripts `.obj`
- Todos os bloqueios críticos de rotas corrigidos

### Correções Aplicadas
| Fix | Descrição |
|-----|-----------|
| **A1** | Store indireto `op=0x10` (`mode=0x42`) — 6 instruções em `lib.obj` eram silenciosamente ignoradas |
| **A2** | Escolha de rota `sys 7` — separado do diálogo `sys 1`; agora trata menu de escolha 1-based (18 pontos de escolha, 10 scripts de rota) |
| **A3** | Reset de variáveis `sys 17` — limpa bancos 0–4 (`0x0000..0x4fff`), preservando banco 5 (sistema) |
| **A4** | Quit `sys 26` — envia `SDL_EVENT_QUIT` |
| **A5** | Espera por clique `sys 32` — bloqueia sem janela de mensagem (preview de uniforme + CG final) |
| **A6** | Espera temporizada `sys 33` — delay `ms` pulável com `TimerSkippable` |
| **A7** | Pan de CG `sys 52` — pan vertical de CG oversized (3 chamadas em `lib.obj`) |
| **A8** | Galeria `sys 209/225/228` — galeria de CGs + sistema de replay de cenas |
| **A9** | Staff roll `sys 223` — créditos finais Credit00..14 + partículas de neve |

### Nativos Adicionais
- `sys 3/27`: Esconder/resetar janela de mensagem
- `sys 18/25`: Título de capítulo/cena para rótulos de save
- `sys 21`: Log de asserção de debug
- `sys 136`: Parar voz (roteia para `sys 132`)

### Build & Teste
- Todos os CTests passam
- Smoke boot sai com código 0 contra assets retail
