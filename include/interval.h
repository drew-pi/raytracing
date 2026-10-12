#pragma once

#include <algorithm>
#include <cassert>
#include <concepts>
#include <limits>

template <std::floating_point T> class Interval {

public:
  T minimum;
  T maximum;

  constexpr Interval() noexcept
      : minimum(std::numeric_limits<T>::infinity()),
        maximum(-std::numeric_limits<T>::infinity()) {}

  constexpr Interval(const T min, const T max) noexcept
      : minimum(min), maximum(max) {}

  constexpr Interval(const Interval<T> &i1, const Interval<T> &i2) noexcept {
    minimum = std::min(i1.min(), i2.min());
    maximum = std::max(i1.max(), i2.max());
  }

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
    return Interval();
  }

  [[nodiscard]] static constexpr Interval universe() noexcept {
    return Interval{-std::numeric_limits<T>::infinity(),
                    std::numeric_limits<T>::infinity()};
  }

  [[nodiscard]] static constexpr Interval standard() noexcept {
    return Interval{0.001, std::numeric_limits<T>::infinity()};
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

  [[nodiscard]] constexpr Interval<T> expand(T delta) const noexcept {
    T padding = delta / T{2};
    return Interval(minimum - padding, maximum + padding);
  }

  [[nodiscard]] constexpr T min() const noexcept { return minimum; }

  [[nodiscard]] constexpr T max() const noexcept { return maximum; }
};
