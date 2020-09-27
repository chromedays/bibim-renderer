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

bool endsWith(const std::string &_str, char _suffix) {
  return (_str.length() >= 1) && (_str[_str.length() - 1] == _suffix);
}

bool endsWith(const std::string &_str, const char *_suffix) {
  size_t suffixLen = strlen(_suffix);
  return (_str.length() >= suffixLen) &&
         (_str.compare(_str.length() - suffixLen, suffixLen, _suffix) == 0);
}

bool endsWith(const std::string &_str, const std::string &_suffix) {
  return (_str.length() >= _suffix.length()) &&
         (_str.compare(_str.length() - _suffix.length(), _suffix.length(),
                       _suffix) == 0);
}

bool contains(const std::string &_str, char _subchar) {
  return _str.find(_subchar) != std::string::npos;
}

bool contains(const std::string &_str, const char *_substr) {
  return _str.find(_substr) != std::string::npos;
}

bool contains(const std::string &_str, const std::string &_substr) {
  return _str.find(_substr) != std::string::npos;
}

} // namespace bb