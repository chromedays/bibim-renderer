#pragma once
#include <type_traits>
#include <stdint.h>

namespace bb {

template <typename E, typename T> struct EnumArray {
  T Elems[(int)(E::COUNT)] = {};

  const T &operator[](E _e) const { return Elems[(int)_e]; }

  T &operator[](E _e) { return Elems[(int)_e]; }

  T *begin() { return Elems; }

  T *end() { return Elems + (size_t)(E::COUNT); }

  static_assert(std::is_enum_v<E>);
  static_assert((int64_t)(E::COUNT) > 0);
};

} // namespace bb