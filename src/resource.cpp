#include "resource.h"
#include "util.h"
#include "external/SDL2/SDL.h"
#include "external/toml.h"
#include <string_view>

namespace bb {

static std::string gCommonResourceRoot;
static std::string gShaderRoot;

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
  joined = simplifyPath(joined);
  return joined;
}

std::string getFileName(std::string_view _path) {
  int i = (int)_path.length() - 1;
  for (; i >= 0; --i) {
    if (isSeparator(_path[i])) {
      break;
    }
  }

  std::string fileName(_path.substr(i + 1));

  return fileName;
}

void initResourceRoot() {
  std::string exeDir;
  {
    char *temp = SDL_GetBasePath();
    exeDir = temp;
    SDL_free(temp);
  }

  if (!endsWith(exeDir, nativePathSeparator)) {
    exeDir += nativePathSeparator;
  }

  std::string configPath = exeDir + "config.toml";
  FILE *configFile = fopen(configPath.c_str(), "r");
  toml_table_t *config = toml_parse_file(configFile, nullptr, 0);
  fclose(configFile);

  toml_table_t *tomlResourcePath = toml_table_in(config, "resource_path");
  auto getString = [&](toml_table_t *_table, const char *_key) {
    toml_raw_t raw = toml_raw_in(_table, _key);
    char *cstr;
    toml_rtos(raw, &cstr);
    std::string str = cstr;
    free(cstr);
    return str;
  };

  gCommonResourceRoot =
      joinPaths(exeDir, getString(tomlResourcePath, "common_root"));
  gShaderRoot = joinPaths(exeDir, getString(tomlResourcePath, "shader_root"));

  toml_free(config);
}

std::string createCommonResourcePath(std::string_view _relPath) {
  std::string absPath = joinPaths(gCommonResourceRoot, _relPath);
  return absPath;
}

std::string createShaderPath(std::string_view _relPath) {
  std::string absPath = joinPaths(gShaderRoot, _relPath);
  return absPath;
}

} // namespace bb