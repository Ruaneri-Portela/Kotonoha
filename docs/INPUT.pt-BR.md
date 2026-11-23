# Referência de Entrada e Controles

[English (US)](INPUT.md) | **Português (Brasil)**

O Kotonoha oferece um pipeline unificado de entrada multi-dispositivo em todos os 15 ports de jogos, no launcher desktop, no `KotonohaLauncher` unificado e no overlay de debug Dear ImGui. Todos os ports suportam **Mouse**, **Touch**, **Teclado** e **Controle (Gamepad)** simultaneamente com suporte a hotplug.

---

## 1. Arquitetura Compartilhada de Entrada

Toda a normalização de entrada vive em `lib/`:

| Componente | Cabeçalho | Responsabilidade |
|---|---|---|
| `SDLSession` / `GamepadManager` | `Kotonoha/core/SDL.hpp`, `Kotonoha/core/InputMapper.hpp` | Inicializa `SDL_INIT_GAMEPAD`, abre controles já conectados e acompanha hotplug via `SDL_EVENT_GAMEPAD_ADDED` / `SDL_EVENT_GAMEPAD_REMOVED` usando handles RAII `GamepadPtr`. |
| `NormalizeControllerEvent` / `MapControllerEvent` | `Kotonoha/events/ControllerEvent.hpp` | Aplica `settings.swapControllerAB` na borda da engine e mapeia botões/gatilhos do controle para ações de teclado conforme o contexto (`Menu`, `Dialogue`, `Backlog`). |
| `ControllerNavigation` | `Kotonoha/events/ControllerEvent.hpp` | Converte os eixos do analógico esquerdo (`SDL_GAMEPAD_AXIS_LEFTX`, `SDL_GAMEPAD_AXIS_LEFTY`) em eventos virtuais de D-pad com deadzone com histerese (`16000` pressionar / `10000` soltar) e liberação segura ao perder foco ou desconectar. |
| `NavRepeater` | `Kotonoha/core/InputMapper.hpp` | Gera eventos de repetição direcional (`380 ms` de atraso inicial, `95 ms` de intervalo de repetição) quando o D-pad ou o analógico esquerdo permanece segurado dentro de `Engine::Iterate()`. |
| `InputMapper` | `Kotonoha/core/InputMapper.hpp` | Mapeia eventos de mouse, teclado e controle para valores semânticos `InputAction` (`Confirm`, `Cancel`, `Menu`, `Backlog`, `AutoToggle`, `SkipHold`, `SkipToggle`, `QuickSave`, `QuickLoad`, `NavUp/Down/Left/Right`, `PageUp/Down`) e fornece conversão de coordenadas via `InputMapper::touchToLogical()`. |
| `InputActions` & `TouchPointer` | `Kotonoha/events/InputActions.hpp` | Traduz `SDL_EVENT_FINGER_DOWN/MOTION/UP/CANCELED` para coordenadas lógicas, toque simples para confirmar/clicar, toque com dois dedos para voltar, deslize da borda esquerda para voltar, toque longo (`550 ms`) para menu/voltar, deslize para cima para backlog e deslize interno para a direita para alternar skip. |
| `ImGuiConfigFlags_NavEnableGamepad` | `lib/src/core/Launcher.cpp`, `lib/src/debug/DebugOverlay.cpp` | Habilita navegação completa por teclado e controle no launcher desktop/unificado e no overlay de debug Dear ImGui. |

---

## 2. Esquema Padrão de Controles

| Ação (`InputAction`) | Mouse | Touch | Teclado | Controle (Padrão / `swapControllerAB`) |
|---|---|---|---|---|
| **Confirmar / Avançar** (`Confirm`) | Clique esquerdo | Toque simples | `Enter` / `Space` / `KP_Enter` | Leste (`B`/Círculo) — ou Sul (`A`/Xis) com `swapControllerAB` ativado |
| **Cancelar / Voltar** (`Cancel`) | Clique direito | Toque com dois dedos, deslize da borda esquerda para a direita ou toque longo (`550 ms`) | `Esc` / `Backspace` / `AC_Back` | Sul (`A`/Xis) em menus, ou Norte (`Y`/Triângulo) |
| **Pausa / Menu do Jogo** (`Menu`) | Clique direito (conforme o port) ou botão de menu mobile | Toque com dois dedos ou toque longo (`550 ms`) | `Esc` | `Start` |
| **Backlog / Histórico** (`Backlog`) | Roda para cima | Deslize para cima | `PageUp` / `B` | `LB` / `Back` (Select) |
| **Modo Automático** (`AutoToggle`) | Botão na barra | Toque na barra | `A` | `RB` |
| **Segurar Skip** (`SkipHold`) | — | — | `LCtrl` / `RCtrl` | `LT` / `RT` (gatilhos analógicos), ou Sul no diálogo |
| **Alternar Skip** (`SkipToggle`) | Botão na barra | Deslize para a direita (interior) | `S` | Oeste (`X`/Quadrado) |
| **Navegar Menus / Escolhas** (`NavUp/Down/Left/Right`) | Hover + Clique esquerdo | Toque / Arrastar | Setas (`Up`/`Down`/`Left`/`Right`) | D-pad ou Analógico Esquerdo (com deadzone e repetição) |
| **Quick Save / Quick Load** (`QuickSave` / `QuickLoad`) | Menu | Menu | `F5` / `F6` (ou `F9`) | Via Menu do Jogo |
| **Alternar Tela Cheia** | — | — | `F11` / `Alt+Enter` | — |

---

## 3. Matriz de Suporte dos Ports e Launcher

Todos os 15 ports de jogos e o launcher unificado passam por `Engine` / `Launcher` e suportam as quatro modalidades de entrada:

| Alvo | Mouse | Touch | Teclado | Controle | Observações |
|---|---|---|---|---|---|
| `KotonohaLauncher` (Desktop & Unificado) | Sim | Sim (`TouchPointer`) | Sim (`ImGuiConfigFlags_NavEnableKeyboard`) | Sim (`SDL_INIT_GAMEPAD`, `GamepadManager`, `ImGuiConfigFlags_NavEnableGamepad`) | Hotplug completo + navegação ImGui por controle |
| `example` (`DemoGame`) | Sim | Sim | Sim | Sim | Usa `MapControllerEvent` + componentes compartilhados de VN |
| `amagami` (`AmagamiRuntime`) | Sim | Sim | Sim | Sim | `VitaInput` nativo + `MapControllerEvent` + navegação de backlog/mapa |
| `zero-no-tsukaima` (`ZeroRuntime`) | Sim | Sim | Sim | Sim | `MapControllerEvent` + suporte em frontend/textbox/backlog |
| `snow-radish-vacation` (`SnowRuntime`) | Sim | Sim | Sim | Sim | `MapControllerEvent` + cenas customizadas e backlog |
| `summer-radish-vacation` (`SummerRuntime`) | Sim | Sim | Sim | Sim | `MapControllerEvent` para Summer, LostM, Imouto e MEO |
| `meo` (`SummerRuntime` modo MEO) | Sim | Sim | Sim | Sim | Título, menu de cenas, overlay e gameplay mapeados via `MapControllerEvent` |
| `doki-doki-literature-club` (`DdlcRuntime`) | Sim | Sim | Sim | Sim | Touch/roda/teclado nativos + `MapControllerEvent` |
| `katawa-shoujo` (`DdlcRuntime`) | Sim | Sim | Sim | Sim | Compartilha o pipeline de entrada de `Ddlc::GameRuntime` |
| `school-days` (`SchoolDays::Runtime`) | Sim | Sim | Sim | Sim | `MapControllerEvent` + `SchoolDaysInputPolicy` + touch nativo |
| `reallive` (`RealLiveRuntime`) | Sim | Sim | Sim | Sim | `MapControllerEvent` + `RealLiveInput` + `TitleInput` + `RealLiveSelection` |
| `kirikiri` (`KirikiriRuntime`) | Sim | Sim | Sim | Sim | `MapControllerEvent` + overlays VNX+ + Ctrl/S/A/PageUp/AC_Back |
| `luca-system` (`LucaSystemRuntime`) | Sim | Sim | Sim | Sim | `MapControllerEvent` + navegação de título/grade de saves/galeria |
| `pmail` (`PmailRuntime` + `LargePonPonRuntime`) | Sim | Sim | Sim | Sim | `MapControllerEvent` / `InputMapper::toKeyEvent` + navegação completa de Título/Escolha/Mapa/CG |
| `n2system` (`N2Runtime`) | Sim | Sim | Sim | Sim | `InputMapper::toKeyEvent` + modais `capturesInput()` + backlog por roda/PageUp + RMB/AC_Back + Ctrl skip |
| `ponscripter` (`PonscripterRuntime`) | Sim | Sim | Sim | Sim | `MapControllerEvent` + backlog PageUp/B + clique direito AC_Back/Esc + Ctrl/S/A |
