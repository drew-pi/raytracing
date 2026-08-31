#pragma once

#include <concepts>
#include <vec3.h>

template <std::floating_point T> using point3 = vec3<T>;

template <std::floating_point T> class ray {

private:
  point3<T> orig;
  vec3<T> dir;

public:
  constexpr ray() noexcept = default;

  constexpr ray(const point3<T> &o, const vec3<T> &d) noexcept
      : orig(o), dir(d) {}

  // uses a const reference so cannot retain the reference after the underlying
  // object is destroyed
  [[nodiscard]] constexpr const point3<T> origin() const noexcept {
    return orig;
  }

  // uses a const reference so cannot retain the reference after the underlying
  // object is destroyed
  [[nodiscard]] constexpr const vec3<T> direction() const noexcept {
    return dir;
  }

  [[nodiscard]] constexpr point3<T> at(T t) const noexcept {
    return orig + t * dir;
  }
};
