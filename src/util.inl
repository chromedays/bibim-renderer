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

} // namespace bb