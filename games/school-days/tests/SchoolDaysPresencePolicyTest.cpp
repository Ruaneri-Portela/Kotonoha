#include "SchoolDaysPresencePolicy.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

int main() {
  using namespace SchoolDays;
  PresenceState state;
  assert(resolvePresence(state, "en") == "Kotonoha Engine");
  state.protectedState = false;
  state.menu = true;
  assert(resolvePresence(state, "pt-BR") == "Menu principal");
  state.options = true;
  assert(resolvePresence(state, "es") == "Opciones");
  state.menu = false;
  state.options = false;
  // Unknown scene cannot disclose an episode, a choice or a final.
  state.choice = true;
  state.finalWindow = true;
  state.endingCode = 6;
  assert(resolvePresence(state, "en") == "Kotonoha Engine");
  state.scene = {true, 3, false};
  assert(resolvePresence(state, "en") == "Blue Smile");
  state.finalWindow = false;
  assert(resolvePresence(state, "pt-BR") == "Fazendo escolhas");
  state.choice = false;
  assert(resolvePresence(state, "ru") == "Эпизод 03");
  assert(resolvePresence(state, "zh-CN") == "第 03 话");
  for (int episode = 1; episode <= 6; ++episode) {
    state.scene.episode = episode;
    assert(resolvePresence(state, "en") != "Kotonoha Engine");
  }
  state.scene.credits = true;
  assert(resolvePresence(state, "en") == "Kotonoha Engine");
  state.finalWindow = true;
  state.protectedState = true;
  assert(resolvePresence(state, "en") == "Kotonoha Engine");
  state.protectedState = false;
  for (int code = 0; code < 22; ++code) {
    state.endingCode = code;
    assert(!resolvePresence(state, "en").empty());
  }
  Kotonoha_orsData empty{};
  assert(!classifyPresenceScene("00/00-00-A00", empty).verified);
  assert(!classifyPresenceScene("unknown", empty).verified);
}
