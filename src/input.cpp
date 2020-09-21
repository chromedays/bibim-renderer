#include "input.h"

namespace bb {
Input::Input() : MouseDown(false), CursorScreenDelta{} {
  SDL_GetMouseState(&CursorScreenPos.X, &CursorScreenPos.Y);
}

void Input::processKeyboardEvents(const SDL_Event &_e) {
  if (_e.type == SDL_KEYDOWN || _e.type == SDL_KEYUP) {
    Keys[_e.key.keysym.sym] = (_e.key.state == SDL_PRESSED);
  }
}

bool Input::isKeyDown(SDL_Keycode _keyCode) const {
  auto found = Keys.find(_keyCode);
  if (found != Keys.end()) {
    return found->second;
  } else {
    return false;
  }
}
} // namespace bb