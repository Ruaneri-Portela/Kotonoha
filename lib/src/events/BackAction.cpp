#include <Kotonoha/events/BackAction.hpp>
#include <unordered_set>

namespace Kotonoha {

bool BackAction(Engine &engine, EventRef event) {
  // 1. Tecla ESC ou botão de Voltar nativo (Android/Mobile)
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_AC_BACK) {
      return true;
    }
  }

  // 2. Clique com botão direito do mouse
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
      event.button.button == SDL_BUTTON_RIGHT) {
    return true;
  }

  // 3. Gestão de dois dedos (Touch multi-touch)
  static std::unordered_set<SDL_FingerID> activeFingers;
  static bool multiTouchTriggered = false;

  if (event.type == SDL_EVENT_FINGER_DOWN) {
    activeFingers.insert(event.tfinger.fingerID);
  } else if (event.type == SDL_EVENT_FINGER_UP) {
    // Se havia 2 ou mais dedos na tela no momento em que um dedo é levantado
    if (activeFingers.size() >= 2 && !multiTouchTriggered) {
      multiTouchTriggered = true;
      activeFingers.erase(event.tfinger.fingerID);
      return true;
    }

    activeFingers.erase(event.tfinger.fingerID);

    // Reseta a flag quando todos os dedos forem removidos da tela
    if (activeFingers.empty()) {
      multiTouchTriggered = false;
    }
  }

  return false;
}

} // namespace Kotonoha