#pragma once

#include <concepts>
#include <vec3.h>

template <std::floating_point T> using point3 = vec3<T>;

template <std::floating_point T> class Ray {

private:
  point3<T> orig;
  vec3<T> dir;
  T t;

  [[nodiscard]] static constexpr T inverse_component(T value) noexcept {
    return value == T{0} ? T{0} : T{1} / value;
  }

  [[nodiscard]] static constexpr vec3<T>
  inverse_of(const vec3<T> &direction) noexcept {
    return vec3<T>(inverse_component(direction.x()),
                   inverse_component(direction.y()),
                   inverse_component(direction.z()));
  }

public:
  constexpr Ray() noexcept = default;

  constexpr Ray(const point3<T> &o, const vec3<T> &d, T t)
      : orig(o), dir(d), t(t) {}

  constexpr Ray(const point3<T> &o, const vec3<T> &d) noexcept
      : Ray(o, d, T{0}) {}

  constexpr Ray(const point3<T> &o) noexcept
      : Ray(o, vec3<T>(T{0}, T{0}, T{0})) {}

  // uses a const reference so after the underlying object is
  // destroyedcannot retain the reference
  [[nodiscard]] constexpr const point3<T> &origin() const noexcept {
    return orig;
  }

  // uses a const reference so after the underlying object is destroyedcannot
  // retain the reference
  [[nodiscard]] constexpr const vec3<T> &direction() const noexcept {
    return dir;
  }

  [[nodiscard]] constexpr T time() const noexcept { return t; }

  [[nodiscard]] constexpr point3<T> at(T t) const noexcept {
    return orig + t * dir;
  }
};
