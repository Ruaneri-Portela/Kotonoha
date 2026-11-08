// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "TitleInput.hpp"
#include <Kotonoha/events/ControllerEvent.hpp>
#include <cassert>
#include <iostream>

namespace {
SDL_Event key(SDL_Keycode code, bool down = true) {
  SDL_Event event{};
  event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  event.key.key = code;
  event.key.down = down;
  return event;
}
bool inside(int x, int y, int left, int right, int top, int bottom) {
  return x > left && x < right && y > top && y < bottom;
}
} // namespace

int main() {
  RealLive::TitleInput title;
  assert(!title.active() && title.focus() == -1);

  // Every virtual target must fall inside the native SEEN 9032 hitboxes,
  // including the separately placed Exit entry.
  for (int i = 0; i < 5; ++i) {
    auto point = RealLive::TitleInput::target(i);
    assert(inside(point.x, point.y, 220, 420, 282 + i * 28, 310 + i * 28));
  }
  auto exit = RealLive::TitleInput::target(5);
  assert(inside(exit.x, exit.y, 552, 630, 447, 479));

  // A bare confirm focuses the first entry and produces a press followed by a
  // release; the script must see exactly one click.
  assert(title.handle(key(SDLK_RETURN)));
  assert(title.active() && title.focus() == 0);
  assert(title.mouseX() == 320 && title.mouseY() == 296);
  assert(title.poll() == 1);
  assert(title.poll() == 2);
  assert(title.poll() == -1);

  // Navigation wraps through all six entries and is repeat-safe.
  title.reset();
  assert(title.handle(key(SDLK_DOWN)));
  assert(title.focus() == 0);
  assert(title.handle(key(SDLK_DOWN)));
  assert(title.focus() == 1);
  assert(title.handle(key(SDLK_UP)));
  assert(title.focus() == 0);
  assert(title.handle(key(SDLK_UP)));
  assert(title.focus() == 5);
  assert(title.handle(key(SDLK_DOWN)));
  assert(title.focus() == 0);
  assert(title.handle(key(SDLK_RIGHT)));
  assert(title.focus() == 1);
  assert(title.handle(key(SDLK_LEFT)));
  assert(title.focus() == 0);
  SDL_Event repeat = key(SDLK_DOWN);
  repeat.key.repeat = true;
  assert(!title.handle(repeat));
  assert(title.focus() == 0);

  // Confirm works from the keyboard and from the gamepad mapping.
  assert(title.handle(key(SDLK_SPACE)));
  assert(title.poll() == 1 && title.poll() == 2 && title.poll() == -1);
  SDL_Event east{};
  east.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  east.gbutton.button = SDL_GAMEPAD_BUTTON_EAST;
  east.gbutton.down = true;
  assert(title.handle(Kotonoha::MapControllerEvent(east)));
  assert(title.poll() == 1 && title.poll() == 2 && title.poll() == -1);

  // A pending click never repeats if confirm is pressed again before the
  // script polls.
  assert(title.handle(key(SDLK_KP_ENTER)));
  assert(title.handle(key(SDLK_RETURN)));
  assert(title.poll() == 1 && title.poll() == 2 && title.poll() == -1);

  // Pointer/touch input returns control to the native mouse handling and
  // cancels any pending synthetic click.
  assert(title.handle(key(SDLK_RETURN)));
  SDL_Event motion{};
  motion.type = SDL_EVENT_MOUSE_MOTION;
  motion.motion.x = 10;
  motion.motion.y = 20;
  assert(!title.handle(motion));
  assert(!title.active() && title.poll() == -1);

  std::cout << "REALLIVE title input tests passed\n";
  return 0;
}
