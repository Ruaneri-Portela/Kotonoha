# School Days HQ Android — matriz de toque (Etapa 6)

**Causa do problema** (auditoria estática): SDL3 entrega toque físico em `SDL_EVENT_FINGER_*`; a sessão desativa `SDL_HINT_TOUCH_MOUSE_EVENTS`. Anteriormente, o motor despachava `FINGER_DOWN` a telas retail que só tratavam `MOUSE_MOTION` e `MOUSE_BUTTON_DOWN`, e sintetizava um clique apenas no `FINGER_UP` sem ter passado movimento. O teste de CMAP exigia `hit == selection`, mas `selection` vinha do hover e ficava desatualizado. Para alguns botões, `tfinger` também recebia transformação extra em coordenadas lógicas, após a conversão do SDL.

## Correção Android, sem alterar a semântica de mouse do PC

- `MobileTouchAdapter` em `lib/src/events/InputActions.cpp/.hpp`, com instância por `Engine`, fica na **única** fronteira de entrada `Engine::Event()` no Android. SDL finger vem normalizado em [0,1], é convertido **uma vez** em coordenadas de janela (SDL `GetWindowSize`); `dispatchEvent` faz as transformações Screen/Novel/letterbox e `UI::toFrame` habituais. Nenhum `FINGER_DOWN` bruto é repassado aos controles retail e não há dupla ativação com `SDL_TOUCH_MOUSEID`.
- **Toque simples:** movimento de ponteiro para selecionar CMAP; ao soltar sem arrasto, emite um movimento e um par mouse DOWN/UP. Isso atende os controles que disparam no DOWN e os que confirmam no UP, inclusive título e escolhas temporizadas.
- **Toque longo (>=600ms):** envia Escape, o equivalente Android à ação de voltar/menu/clique direito. Nunca envia também o clique esquerdo.
- **Botão MENU visível:** o próprio School Days desenha uma área de toque no canto inferior esquerdo (não sobre a barra superior retail); o toque abre Config nativa e não instancia o `InGameMenu` genérico que não é uma cena do jogo.
- **Gesto da borda esquerda:** deslizar da esquerda para a direita além do limiar envia Escape uma vez; evita tap posterior.
- **Arrasto vertical:** produz passos de roda a cada fração de tela percorrida; BackLog histórico avança ou recua em registros; Save/Load e Replay mudam páginas. Seleções de slots não são ativadas durante arrasto. Um segundo dedo/cancel/foco perdido desativa a ativação pendente.
- **Direção e velocidade do swipe:** arrastar para CIMA emite roda negativa (avança o conteúdo/página); para BAIXO, roda positiva (retorna). Um movimento grande gera até oito passos proporcionais, sem descartar distância acumulada. A troca de página respeita as travas de fade e carregamento antes de modificar o modelo.
- O botão Voltar físico (`SDLK_AC_BACK`) permanece mapeado ao caminho de Escape já implementado na runtime, abrindo/cancelando a tela modal correta.
- Todas as alterações de roteamento e gestos estão sob `#if __ANDROID__/ANDROID`; Windows mantém as condições de hover, mouse e hit-test originais. Lógica de conteúdo, timings, traduções e saves não foi alterada.
- **Aba Display interna:** removido o filtro legado que descartava os novos cliques sintéticos Android no painel; seleção/presets recebem o mesmo movimento+clique que General/Sound.
- **Settings do motor:** quando está aberto, os eventos de slider passam a ter press, movimento contínuo e release; swipe vertical ainda emite wheel e não aciona um item após arrasto. Release de slider não atravessa para outro controle.

## Matriz de telas e verificação

| Tela/aba | Ação por toque roteada | Estado |
|---|---|---|
| Splash / logo | Tap libera splash | Roteamento Android adaptado; **não testado** em dispositivo |
| Title / New Game / Exit | Tap com motion antes de DOWN/UP; Voltar | Adaptado; **não testado** |
| Escolhas temporizadas | Tap único e sem dupla confirmação | Adaptado; **não testado**, verificar timeout natural |
| Gameplay / barra, Auto/Skip/1×–32× | Tap arma seleção CMAP no movimento antes do DOWN; botão MENU visível abre Config do School Days | Corrigido no código; **não testado** |
| Save / Load / comentários | Tap no slot/abas; swipe muda páginas; Voltar | Adaptado; **não testado** |
| Config General/Sound | Tap nos botões/abas e Voltar | Adaptado; **não testado** |
| Display Options interna | Tap na linha/popup (sem o filtro legado de toque duplicado); swipe para a lista de modos | Adaptado; **não testado** |
| Replay HScene e PlayData | Tap em controles/thumbnail; swipe muda página; Voltar | Adaptado; **não testado** |
| BackLog retail e fallback | Tap nos controles; arrasto percorre histórico; Voltar | Adaptado; **não testado** |
| Route Map | Tap em nó/retornar; Voltar | Adaptado; **não testado** |
| SaveFeel / countdown L×H | Tap em resultado; timeout preservado | Adaptado; **não testado** |
| Menus genéricos Settings/overlays | Tap, slider com drag, swipe em listas, cancelamento de release arrastado | Adaptado; **não testado** |

**Provas disponíveis:** compilação Java/C++ nativa da variante DEV para ARM64 bem-sucedida; inspeção de fluxo de eventos e guardas Android. **Não disponível:** teste físico de toque, 180°/notch/DPI, latência, áudio/video, verificações de duplo clique, configurações/gestos em aparelho. `adb` estava em estado `unauthorized`, logo nenhuma tela foi marcada como OK em hardware.

**Checklist manual mínimo:** liberar depuração USB e autorizar adb no celular (ou usar emulador); tocar em Title/New, escolhas com e sem timeout, abrir Config→General/Sound/Display e testar popups, navegar Save/Load e Replay por toque e swipe, percorrer BackLog com drag, Route Map, Auto/Skip e menu, pressionar Voltar; repetir com 4:3, 16:9, notch, 180° e multitoque/cancel; confirmar que um toque causa apenas uma ativação e não altera saves prévios.
