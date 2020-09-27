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

template <typename T> uint32_t sizeBytes32(const T &_container) {
  return (uint32_t)(sizeof(typename T::value_type) * _container.size());
}

template <typename T> int32_t ssizeBytes32(const T &_container) {
  return (int32_t)(sizeof(typename T::value_type) * _container.size());
}

} // namespace bb