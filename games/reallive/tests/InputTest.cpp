// Copyright 2026 Kotonoha contributors. SPDX-License-Identifier: MIT
#include "RealLiveInput.hpp"
#include "TitleInput.hpp"
#include <iostream>
#include <stdexcept>

void require(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
SDL_Event key(Uint32 type, SDL_Keycode keycode) {
  SDL_Event e{};
  e.type = type;
  e.key.key = keycode;
  return e;
}
SDL_Event button(Uint32 type, SDL_JoystickID id) {
  SDL_Event e{};
  e.type = type;
  e.gbutton.which = id;
  e.gbutton.button = SDL_GAMEPAD_BUTTON_SOUTH;
  return e;
}
SDL_Event axis(SDL_JoystickID id, Uint8 which, Sint16 value) {
  SDL_Event e{};
  e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
  e.gaxis.which = id;
  e.gaxis.axis = which;
  e.gaxis.value = value;
  return e;
}
SDL_Event finger(Uint32 type, SDL_FingerID id, float x, float y) {
  SDL_Event e{};
  e.type = type;
  e.tfinger.touchID = 1;
  e.tfinger.fingerID = id;
  e.tfinger.x = x;
  e.tfinger.y = y;
  return e;
}
int main() {
  bool readSkip = true, autoMode = false;
  RealLive::updateAdvanceModes(false, true, autoMode, readSkip);
  require(readSkip, "Read skip must persist after a read page");
  RealLive::updateAdvanceModes(false, false, autoMode, readSkip);
  require(!readSkip, "Automatic advance may clear read skip");
  readSkip = autoMode = true;
  RealLive::updateAdvanceModes(true, false, autoMode, readSkip);
  require(!readSkip && !autoMode, "Manual advance disables both modes");
  RealLive::RealLiveInput i;
  i.processSkip(key(SDL_EVENT_KEY_DOWN, SDLK_LCTRL), true);
  i.processSkip(key(SDL_EVENT_KEY_DOWN, SDLK_RCTRL), true);
  i.processSkip(key(SDL_EVENT_KEY_UP, SDLK_LCTRL), true);
  require(i.skipHeld(), "One Ctrl release must not release other Ctrl");
  i.processSkip(key(SDL_EVENT_KEY_UP, SDLK_RCTRL), true);
  require(!i.skipHeld(), "All Ctrl keys released");
  require(!i.processSkip(button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1), false),
          "South is menu cancel outside dialogue");
  require(!i.skipHeld(), "Menu must not start skip");
  i.processSkip(button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1), true);
  i.processSkip(axis(1, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 30000), true);
  i.processSkip(axis(2, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 30000), true);
  i.processSkip(button(SDL_EVENT_GAMEPAD_BUTTON_UP, 1), true);
  i.processSkip(axis(1, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0), true);
  require(i.skipHeld(), "Other pad still holding skip");
  SDL_Event removed{};
  removed.type = SDL_EVENT_GAMEPAD_REMOVED;
  removed.gdevice.which = 1;
  i.processSkip(removed, true);
  require(i.skipHeld(), "Removing pad 1 must not reset pad 2");
  i.processSkip(axis(2, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0), true);
  require(!i.skipHeld(), "All triggers released");
  i.processSkip(button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1), true);
  SDL_Event focus{};
  focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  i.processSkip(focus, true);
  require(!i.skipHeld(), "Focus loss clears held skip");
  i.processSkip(button(SDL_EVENT_GAMEPAD_BUTTON_DOWN, 1), true);
  i.resetSkip();
  require(!i.skipHeld(), "Modal clears held skip");

  SDL_Event synthetic{};
  synthetic.type = SDL_EVENT_MOUSE_BUTTON_UP;
  synthetic.button.which = SDL_TOUCH_MOUSEID;
  synthetic.button.button = SDL_BUTTON_LEFT;
  i.acceptTouch(finger(SDL_EVENT_FINGER_DOWN, 10, .4f, .4f));
  i.acceptTouch(finger(SDL_EVENT_FINGER_UP, 10, .4f, .4f));
  require(i.acceptTouch(synthetic), "Tap accepted");
  i.acceptTouch(finger(SDL_EVENT_FINGER_DOWN, 11, .4f, .4f));
  i.acceptTouch(finger(SDL_EVENT_FINGER_MOTION, 11, .7f, .4f));
  i.acceptTouch(finger(SDL_EVENT_FINGER_UP, 11, .7f, .4f));
  require(!i.acceptTouch(synthetic), "Drag rejected");
  i.acceptTouch(finger(SDL_EVENT_FINGER_DOWN, 12, .4f, .4f));
  i.acceptTouch(finger(SDL_EVENT_FINGER_DOWN, 13, .6f, .4f));
  i.acceptTouch(finger(SDL_EVENT_FINGER_UP, 12, .4f, .4f));
  i.acceptTouch(finger(SDL_EVENT_FINGER_UP, 13, .6f, .4f));
  require(!i.acceptTouch(synthetic), "Multi-touch rejected");
  i.acceptTouch(finger(SDL_EVENT_FINGER_DOWN, 14, .4f, .4f));
  i.acceptTouch(finger(SDL_EVENT_FINGER_CANCELED, 14, .4f, .4f));
  require(!i.acceptTouch(synthetic), "Canceled finger rejected");

  RealLive::TitleInput title;
  title.pointer(true, 320, 296);
  title.handle(finger(SDL_EVENT_FINGER_DOWN, 20, .5f, .5f));
  title.handle(finger(SDL_EVENT_FINGER_UP, 20, .5f, .5f));
  title.pointer(false, 320, 296);
  require(title.poll() == 1 && title.poll() == 2,
          "Title click must survive native finger events");
  std::cout << "RealLive input regressions passed\n";
}
