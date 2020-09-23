#pragma once
#include "vector_math.h"
#include "external/SDL2/SDL.h"
#include <unordered_map>

namespace bb {

struct Input {
  std::unordered_map<SDL_Keycode, bool> Keys;

  bool MouseDown;
  Int2 CursorScreenPos;
  Int2 CursorScreenDelta;

  Input();

  void processKeyboardEvents(const SDL_Event &_e);
  bool isKeyDown(SDL_Keycode _keyCode) const;
};

} // namespace bb