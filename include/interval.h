#pragma once

#include <cassert>
#include <concepts>
#include <limits>

template <std::floating_point T> class Interval {

private:
  T minimum;
  T maximum;

public:
  constexpr Interval() noexcept
      : minimum(T{0}), maximum(std::numeric_limits<T>::infinity()) {}

  constexpr Interval(const T min, const T max) noexcept
      : minimum(min), maximum(max) {}

  [[nodiscard]] constexpr bool is_empty() const noexcept {
    return minimum > maximum;
  }

  [[nodiscard]] constexpr bool contains(T t) const noexcept {
    return minimum <= t && t <= maximum;
  }

  [[nodiscard]] constexpr bool surrounds(T t) const noexcept {
    return minimum < t && t < maximum;
  }

  [[nodiscard]] constexpr T size() const noexcept { return maximum - minimum; }

  [[nodiscard]] static constexpr Interval empty() noexcept {
    return Interval{std::numeric_limits<T>::infinity(),
                    -std::numeric_limits<T>::infinity()};
  }

  [[nodiscard]] static constexpr Interval universe() noexcept {
    return Interval{-std::numeric_limits<T>::infinity(),
                    std::numeric_limits<T>::infinity()};
  }

  [[nodiscard]] constexpr T clamp(T t) const noexcept {
    assert(!is_empty());

    if (t < minimum) {
      return minimum;
    }
    if (t > maximum) {
      return maximum;
    }
    return t;
  }

  [[nodiscard]] constexpr T min() const noexcept { return minimum; }

  [[nodiscard]] constexpr T max() const noexcept { return maximum; }
};
