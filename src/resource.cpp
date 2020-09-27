#include "resource.h"
#include "util.h"
#include "external/SDL2/SDL.h"
#include <string_view>

namespace bb {

static std::string gResourceRoot;

static bool isSeparator(char _ch) { return (_ch == '\\') || (_ch == '/'); }

static void replaceSeparatorsWithNative(std::string &_str, int _offset = 0) {
  std::replace_if(_str.begin() + _offset, _str.end(), isSeparator,
                  nativePathSeparator);
}

static std::string_view trimSeparators(std::string_view _str) {
  bool isValidPath = false;
  int trimBegin = 0;
  int trimEnd = 0;
  for (int i = 0; i < (int)_str.length(); ++i) {
    if (!isSeparator(_str[i])) {
      trimBegin = i;
      isValidPath = true;
      break;
    }
  }
  if (!isValidPath) {
    return {};
  }
  for (int i = (int)_str.length() - 1; i >= 0; --i) {
    if (!isSeparator(_str[i])) {
      trimEnd = i + 1;
      break;
    }
  }

  return _str.substr(trimBegin, trimEnd - trimBegin);
}

// Removes ".." s
static std::string simplifyPath(std::string_view _path) {
  std::vector<std::string_view> dirs;
  dirs.reserve(_path.size());

  int lastAppendedIndex = 0;
  for (int i = 0; i <= (int)_path.length(); ++i) {
    if (i == _path.length() || isSeparator(_path[i])) {
      std::string_view dir =
          _path.substr(lastAppendedIndex, i - lastAppendedIndex);
      lastAppendedIndex = i + 1;
      if (dir == "..") {
        dirs.pop_back();
      } else {
        dirs.push_back(dir);
      }
    }
  }

  std::string result;
  result.reserve(_path.size());
  size_t i = 0;
  for (const std::string_view &dir : dirs) {
    result += dir;
    if (i < dirs.size() - 1) {
      result += nativePathSeparator;
    }
    ++i;
  }

  return result;
}

std::string createAbsolutePath(std::string_view _relPath) {
  std::string absPathRaw = gResourceRoot;
  if (!_relPath.empty()) {
    absPathRaw += nativePathSeparator;
    absPathRaw += trimSeparators(_relPath);
  }
  replaceSeparatorsWithNative(absPathRaw);

  std::string result = simplifyPath(absPathRaw);

  return result;
}

bool isAbsolutePath(std::string_view _path) {
  return (_path.length() >= 2) && (_path[1] == ':');
}

std::string joinPaths(std::string_view _a, std::string_view _b) {
  std::string joined;
  joined.reserve((_a.size() + _b.size()) * 2);
  _a = trimSeparators(_a);
  _b = trimSeparators(_b);
  joined = _a;
  joined += nativePathSeparator;
  joined += _b;
  replaceSeparatorsWithNative(joined);
  return joined;
}

std::string getFileName(std::string_view _path) {
  std::string absPath = createAbsolutePath(_path);

  int i = (int)absPath.length() - 1;
  for (; i >= 0; --i) {
    if (isSeparator(absPath[i])) {
      break;
    }
  }

  std::string fileName = absPath.substr(i + 1);

  return fileName;
}

void initResourceRoot() {
  char *rawResourceRoot = SDL_GetBasePath();

  gResourceRoot = rawResourceRoot;
  if (!endsWith(gResourceRoot, nativePathSeparator)) {
    gResourceRoot += nativePathSeparator;
  }
  gResourceRoot += "..";
  gResourceRoot += nativePathSeparator;
  gResourceRoot += "..";
  gResourceRoot += nativePathSeparator;
  gResourceRoot += "resources";
  gResourceRoot = simplifyPath(gResourceRoot);

  SDL_free(rawResourceRoot);
}

} // namespace bb