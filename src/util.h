#pragma once
#include "external/fmt/format.h"
#include <string>
#include <chrono>
#ifdef BB_WINDOWS
#include <Windows.h>
#endif

#ifdef BB_DEBUG
#ifdef BB_WINDOWS
#define BB_ASSERT(exp)                                                         \
  do {                                                                         \
    if (!(exp)) {                                                              \
      BB_LOG_ERROR("ASSERT TRIGGERED: {}", #exp);                              \
      __debugbreak();                                                          \
    }                                                                          \
  } while (0)
#else
#define BB_ASSERT(exp) assert(exp)
#endif
#define BB_LOG_INFO(...) bb::log(bb::LogLevel::Info, __VA_ARGS__)
#define BB_LOG_WARNING(...) bb::log(bb::LogLevel::Warning, __VA_ARGS__)
#define BB_LOG_ERROR(...) bb::log(bb::LogLevel::Error, __VA_ARGS__)
#else
#define BB_ASSERT(exp)
#define BB_LOG_INFO(...)
#define BB_LOG_WARNING(...)
#define BB_LOG_ERROR(...)
#endif
#define BB_VK_ASSERT(exp)                                                      \
  do {                                                                         \
    auto __result__ = exp;                                                     \
    BB_ASSERT(__result__ == VK_SUCCESS);                                       \
  } while (0)

#define BB_DO_STRING_JOIN(a, b) a##b
#define BB_STRING_JOIN(a, b) BB_DO_STRING_JOIN(a, b)
#define BB_DEFER(code)                                                         \
  bb::ScopeGuard BB_STRING_JOIN(scope_guard_, __LINE__)([&]() { code; })

namespace bb {

enum class LogLevel { Info, Warning, Error };

void printString(const std::string &_str);
void printString(const char *_str);
template <typename... Args> void printLine(Args... args);
template <typename... Args> void log(LogLevel level, Args... args);

template <typename T> uint32_t sizeBytes32(const T &_container);

using Time = std::chrono::time_point<std::chrono::high_resolution_clock>;

Time getCurrentTime();
float getElapsedTimeInSeconds(Time _start, Time _end);

bool endsWith(const std::string &_str, char _suffix);
bool endsWith(const std::string &_str, const char *_suffix);
bool endsWith(const std::string &_str, const std::string &_suffix);

bool contains(const std::string &_str, char _subchar);
bool contains(const std::string &_str, const char *_substr);
bool contains(const std::string &_str, const std::string &_substr);

template <typename Fn> struct ScopeGuard {
  Fn Func;
  bool Active;

  ScopeGuard(const Fn &_func);
  ScopeGuard(Fn &&_func);
  ScopeGuard(ScopeGuard &&_other);
  ~ScopeGuard();
  ScopeGuard(const ScopeGuard &) = delete;
  ScopeGuard &operator=(const ScopeGuard &) = delete;
  ScopeGuard &operator=(ScopeGuard &&) = delete;
};

struct FileData {
  uint32_t Size;
  uint8_t* Contents;
};

FileData readEntireFile(std::string_view _filePath);
void destroyFileData(FileData& _fileData);

} // namespace bb

#include "util.inl"