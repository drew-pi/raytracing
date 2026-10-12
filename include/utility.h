#pragma once

#include <concepts>
#include <numbers>
#include <random>

template <std::floating_point T> constexpr T degree_to_radians(T degrees) {
  return degrees * std::numbers::pi_v<T> / T{180};
}

template <std::floating_point T> [[nodiscard]] T random_real() {
  static std::uniform_real_distribution<T> distribution(0.0, 1.0);
  static std::mt19937 generator;
  return distribution(generator);
}

template <std::floating_point T> [[nodiscard]] T random_real(T min, T max) {
  return min + (max - min) * random_real<T>();
}

inline int random_int(int min, int max) {
  // Returns a random integer in [min,max].
  return min + static_cast<int>((max - min + 1) * random_real<float>());
}
