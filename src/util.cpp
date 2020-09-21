#include "util.h"

namespace bb {

void printString(const std::string &_str) {
  OutputDebugStringA(_str.c_str());
  printf("%s", _str.c_str());
}

void printString(const char *_str) {
  OutputDebugStringA(_str);
  printf("%s", _str);
}

Time getCurrentTime() { return std::chrono::high_resolution_clock::now(); }

static_assert(sizeof(Time) <= sizeof(Time *));
float getElapsedTimeInSeconds(Time _start, Time _end) {
  float result = (float)(std::chrono::duration_cast<std::chrono::milliseconds>(
                             _end - _start)
                             .count()) /
                 1000.f;
  return result;
}

} // namespace bb