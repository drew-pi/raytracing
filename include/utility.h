#pragma once

#include <pcg_random.hpp>

#include <cassert>
#include <concepts>
#include <cstdint>
#include <numbers>
#include <random>

template <std::floating_point T> constexpr T degree_to_radians(T degrees) {
  return degrees * std::numbers::pi_v<T> / T{180};
}

namespace rng {

// Each thread owns its state. Define a default seed for each worker before its
// first random draw.
inline thread_local pcg32 engine{12345, 0};

inline void seed(std::uint64_t global_seed, std::uint64_t stream = 0) {
  // pcg32 supports 2^63 distinct stream selectors.
  assert(stream < (std::uint64_t{1} << 63));
  engine.seed(global_seed, stream);
}

} // namespace rng

// Returns a uniformly distributed real value in [0, 1).
template <std::floating_point T> [[nodiscard]] T random_real() {
  return std::uniform_real_distribution<T>(T{0}, T{1})(rng::engine);
}

// Requires finite bounds and a representable range, with min < max.
// Equal bounds return that value without consuming randomness.
template <std::floating_point T> [[nodiscard]] T random_real(T min, T max) {
  assert(min <= max);

  if (min == max)
    return min;

  return std::uniform_real_distribution<T>(min, max)(rng::engine);
}

inline int random_int(int min, int max) {
  // Returns a random integer in [min,max].
  assert(min <= max);
  return min + static_cast<int>((max - min + 1) * random_real<float>());
}
