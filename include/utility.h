#pragma once

#include <concepts>
#include <numbers>

template <std::floating_point T>
inline constexpr T degree_to_radians(T degrees) {
  return degrees * std::numbers::pi / T{180};
}
