#include <type_traits>
#include <iterator>

namespace bb {
template <typename... Args> void printLine(Args... args) {
  std::string formatted = fmt::format(args...);
  formatted += "\n";
  printString(formatted);
}

template <typename... Args> void log(LogLevel level, Args... args) {
  switch (level) {
  case LogLevel::Info:
    printString("[Info]:    ");
    break;
  case LogLevel::Warning:
    printString("[Warning]: ");
    break;
  case LogLevel::Error:
    printString("[Error]:   ");
    break;
  }

  printLine(args...);
}

#define ELEMENT_TYPE(container)                                                \
  typename std::iterator_traits<decltype(std::cbegin(container))>::value_type

template <typename Container>
uint32_t sizeBytes32(const Container &_container) {
  return (uint32_t)(sizeof(ELEMENT_TYPE(_container)) * std::size(_container));
}

template <typename Fn>
ScopeGuard<Fn>::ScopeGuard(const Fn &_func) : Func(_func), Active(true) {}
template <typename Fn>
ScopeGuard<Fn>::ScopeGuard(Fn &&_func) : Func(std::move(_func)), Active(true) {}
template <typename Fn>
ScopeGuard<Fn>::ScopeGuard(ScopeGuard &&_other)
    : Func(std::move(other.Func)), Active(_other.Active) {
  other.Active = false;
}
template <typename Fn> ScopeGuard<Fn>::~ScopeGuard() {
  if (Active)
    Func();
}

} // namespace bb