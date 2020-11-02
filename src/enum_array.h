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

  const T *begin() const { return Elems; }

  const T *end() const { return Elems + (size_t)(E::COUNT); }

  size_t size() const { return (size_t)(E::COUNT); }

  T *data() { return Elems; }

  const T *data() const { return Elems; }

  static_assert(std::is_enum_v<E>);
  static_assert((int64_t)(E::COUNT) > 0);
};

// Simple helper struct for iterating through all values of a strong enum type.
// Useful especially for range based for loop.
// e.g.
// for (SomeEnum type : AllEnums<SomeEnum>)
template <typename E> struct AllEnumsWithIndexImpl {
  static_assert(std::is_enum_v<E>, "The type E is not enum");

  using UnderlyingType = std::underlying_type_t<E>;

  struct Iterator {
    UnderlyingType Index;
    E Enum;

    Iterator &operator++() {
      ++Index;
      Enum = (E)Index;
      return *this;
    }

    constexpr bool operator==(Iterator other) { return Index == other.Index; }
    constexpr bool operator!=(Iterator other) { return !(*this == other); }
    constexpr Iterator operator*() { return *this; }
  };

  constexpr Iterator begin() const { return {}; };
  constexpr Iterator end() const {
    return {(UnderlyingType)E::COUNT, E::COUNT};
  }
};

template <typename E>
static constexpr auto AllEnumsWithIndex = AllEnumsWithIndexImpl<E>();

template <typename E> struct AllEnumsImpl {
  static_assert(std::is_enum_v<E>, "The type E is not enum");

  using UnderlyingType = std::underlying_type_t<E>;

  struct Iterator {
    UnderlyingType Index;

    Iterator &operator++() {
      ++Index;
      return *this;
    }

    constexpr bool operator==(Iterator other) { return Index == other.Index; }
    constexpr bool operator!=(Iterator other) { return !(*this == other); }
    constexpr E operator*() { return (E)Index; }
  };

  constexpr Iterator begin() const { return {}; };
  constexpr Iterator end() const { return {(UnderlyingType)(E::COUNT)}; }
};

template <typename E> static constexpr auto AllEnums = AllEnumsImpl<E>();

template <typename E> struct EnumCountImpl {
  static_assert(std::is_enum_v<E>, "The type E is not enum");
  static constexpr auto Value = (std::underlying_type_t<E>)(E::COUNT);
};

template <typename E>
static constexpr auto EnumCount = EnumCountImpl<E>::Value;

} // namespace bb