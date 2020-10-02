#include "util.h"

namespace bb {

void printString(std::string_view _str) {
  OutputDebugStringA(_str.data());
  printf("%s", _str.data());
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

bool endsWith(std::string_view _str, char _suffix) {
  return (_str.length() >= 1) && (_str[_str.length() - 1] == _suffix);
}

bool endsWith(std::string_view _str, std::string_view _suffix) {
  return (_str.length() >= _suffix.length()) &&
         (_str.compare(_str.length() - _suffix.length(), _suffix.length(),
                       _suffix) == 0);
}

bool contains(std::string_view _str, char _subchar) {
  return _str.find(_subchar) != std::string::npos;
}

bool contains(std::string_view _str, std::string_view _substr) {
  return _str.find(_substr) != std::string::npos;
}

} // namespace bb