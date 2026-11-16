# Entrada unificada: teclado, toque e gamepad

[English (US)](INPUT_CONTROLS.md)

O Kotonoha utiliza **eventos SDL3** como fronteira única entre o sistema
operacional, o motor e os runtimes dos jogos. A entrada é organizada em
`lib/include/Kotonoha/events/` e `lib/src/events/`; a interpretação
específica fica em `games/<slug>/`, nunca nas VMs ou nos providers.

## Encaminhamento

1. `SDLSession` abre gamepads reconhecidos e acompanha conexões e remoções.
2. `Engine::Event` normaliza a preferência A/B, converte o stick esquerdo
   em eventos virtuais do direcional e os encaminha. A desconexão ou a
   perda de foco libera direções que ainda estavam pressionadas.
3. `InputActions` reconhece gestos de navegação. `TouchPointer` pode
   converter um dedo ativo em eventos de mouse para interfaces nativas.
4. `HandleNavigation` trata Voltar, histórico e menu quando não existe
   uma cena modal capturando a entrada. Start usa o mesmo fluxo do Esc.
5. O runtime recebe o evento original; controles nativos usam
   `MapControllerEvent` e `TouchActivation` quando apropriado.

Todo manipulador deve tratar `SDL_EVENT_GAMEPAD_REMOVED` e
`SDL_EVENT_WINDOW_FOCUS_LOST` como cancelamento da entrada, não como clique.

## Controles padrão

| Entrada | Comportamento compartilhado |
| --- | --- |
| Enter / gamepad East | Confirmar ou avançar |
| Esc / gamepad Start | Voltar ou abrir o menu durante o jogo |
| Voltar do Android / gamepad North / botão direito do mouse | Voltar |
| Gamepad South | Cancelar; segurar para pular em diálogos compatíveis |
| Gamepad West | Alternar pulo de texto já lido |
| Gamepad Right shoulder | Alternar modo automático |
| Gamepad Back ou Left shoulder | Abrir histórico; fechar se já estiver aberto |
| Direcional ou stick esquerdo | Navegar por menus |
| Gatilhos esquerdo/direito | Pular em diálogos compatíveis |
| Toque simples | Ativar/avançar, conforme a cena |
| Deslizar a partir da borda esquerda / toque com dois dedos | Voltar |
| Deslizar para cima | Abrir histórico |
| Arrastar lista rolável | Rolar sem ativar um item |

`swapControllerAB` é uma preferência que troca os botões físicos South e
East *antes* do processamento pelos runtimes e controles. O mapeamento
padrão não implica que todos os jogos ofereçam toda ação em qualquer estado.

Os runtimes nativos que precisam do estado contínuo de pressionar/soltar
sobrescrevem `Runtime::continuousTouch()`; o motor encaminha então
pressionamento, movimento e liberação do ponteiro sintetizado. As cenas
usuais de visual novel trabalham com ativação por toque simples.

## Matriz de testes

Compile e execute os testes C++ de `lib/tests/`, especialmente
`InputActionsTests.cpp`, `NativeInputTests.cpp` e
`InfrastructureTests.cpp`. Verifique ao menos:

- Desconectar e reconectar com o stick pressionado
- Perder o foco com stick ou gatilho pressionado
- Soltar um entre dois gatilhos, inclusive de gamepads diferentes
- Toque simples, gesto de borda, histórico, dois dedos e arrasto cancelado
- Trocar A/B com um controle conectado
- Abrir/fechar menus com um campo de texto em edição

Execute a suíte normal com:
```sh
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
  ctest --test-dir build --output-on-failure
```

Os testes headless **não substituem** a verificação em aparelhos reais com
Android/iOS, gamepads desktop ou navegadores móveis. Valide cada integração
da plataforma antes de considerar o suporte completo.
