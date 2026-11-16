# Unified input: keyboard, touch and gamepad

[Português (Brasil)](INPUT_CONTROLS.pt-BR.md)

Kotonoha uses **SDL3 events** as the single boundary between the operating
system, the engine and individual game runtimes. Input is organized in
`lib/include/Kotonoha/events/` and `lib/src/events/`; game-specific
interpretation stays in `games/<slug>/`, not the VM or asset provider.

## Routing

1. `SDLSession` opens recognized gamepads and handles device hotplug.
2. `Engine::Event` normalizes the optional controller A/B preference,
   converts left-stick directions to virtual D-pad events and forwards them.
   A disconnect or focus loss releases held virtual directions.
3. `InputActions` detects touch navigation gestures. `TouchPointer` can
   translate an active finger into mouse events for native mouse-style UIs.
4. `HandleNavigation` handles common Back, history and pause/menu commands
   when gameplay has no modal scene capturing input. The Start button follows
   the same route as Escape.
5. The runtime receives its original event; native controls use
   `MapControllerEvent` and `TouchActivation` where appropriate.

All event handlers must treat `SDL_EVENT_GAMEPAD_REMOVED` and
`SDL_EVENT_WINDOW_FOCUS_LOST` as input cancellation, not as button presses.

## Default controls

| Input | Shared default |
| --- | --- |
| Enter / gamepad East | Confirm or advance |
| Escape / gamepad Start | Back or open the gameplay menu |
| Android Back / gamepad North / right mouse click | Back |
| Gamepad South | Cancel; hold skip in supported dialogue contexts |
| Gamepad West | Toggle read-skip |
| Gamepad Right shoulder | Toggle auto mode |
| Gamepad Back or Left shoulder | Open history; close it when already in history |
| D-pad or left stick | Menu navigation |
| Gamepad Left/Right trigger | Skip in supported dialogue contexts |
| One-finger tap | Activate/advance (depending on active scene) |
| Swipe inward from the left edge / two-finger tap | Back |
| Swipe upward | Open backlog |
| Drag on a scrollable list | Scroll without activating an item |

`swapControllerAB` is a user setting that swaps physical South and East
*before* game runtimes and controls receive the event. The default mapping
is not a guarantee that every game supports every action in every state.

Native script runtimes requiring pressed/released touch state override
`Runtime::continuousTouch()`; the engine then forwards synthesized pointer
presses, motion and release. Ordinary novel scenes use tap activation.

## Test matrix

For regression coverage, build and run the C++ test targets under
`lib/tests/`, especially `InputActionsTests.cpp`,
`NativeInputTests.cpp` and `InfrastructureTests.cpp`. Verify at least:

- Disconnect with the stick held, then reconnect
- Lose focus while moving the stick or holding a trigger
- Release one of two triggers, including on separate gamepads
- Touch tap, edge swipe, backlog swipe, two-finger tap and canceled drag
- Change the A/B mapping with a controller connected
- Open and close menus while a text input field is focused

Run the normal test suite with:
```sh
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy \
  ctest --test-dir build --output-on-failure
```

Headless regression tests are **not** a substitute for physical device
verification on Android/iOS, desktop controllers and mobile browsers. Test
platform integration separately before declaring a device fully supported.
