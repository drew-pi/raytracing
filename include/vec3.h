#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <limits>
#include <ostream>

#include <utility.h>

template <std::floating_point T> class vec3 {

private:
  std::array<T, 3> e{};

public:
  constexpr vec3() noexcept = default;

  constexpr vec3(T x, T y, T z) noexcept : e{x, y, z} {}

  [[nodiscard]] constexpr T x() const noexcept { return e[0]; }
  [[nodiscard]] constexpr T y() const noexcept { return e[1]; }
  [[nodiscard]] constexpr T z() const noexcept { return e[2]; }

  [[nodiscard]] constexpr vec3 operator-() const noexcept {
    return vec3(-e[0], -e[1], -e[2]);
  }

  constexpr T operator[](std::size_t i) const noexcept {
    assert(i < e.size());
    return e[i];
  }
  constexpr T &operator[](std::size_t i) noexcept {
    assert(i < e.size());
    return e[i];
  }

  constexpr vec3 &operator+=(const vec3 &v) noexcept {
    e[0] += v.e[0];
    e[1] += v.e[1];
    e[2] += v.e[2];
    return *this;
  }

  constexpr vec3 &operator*=(const vec3 &v) noexcept {
    e[0] *= v.e[0];
    e[1] *= v.e[1];
    e[2] *= v.e[2];
    return *this;
  }

  constexpr vec3 &operator*=(T t) noexcept {
    e[0] *= t;
    e[1] *= t;
    e[2] *= t;
    return *this;
  }

  constexpr vec3 &operator/=(T t) noexcept {
    assert(t != T{0});
    return *this *= 1 / t;
  }

  [[nodiscard]] constexpr T squared_norm() const noexcept {
    return e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
  }

  [[nodiscard]] T length() const noexcept { return std::sqrt(squared_norm()); }

  [[nodiscard]] static vec3<T> random() {
    return vec3(random_real<T>(), random_real<T>(), random_real<T>());
  }

  [[nodiscard]] static vec3<T> random(T min, T max) {
    return vec3(random_real<T>(min, max), random_real<T>(min, max),
                random_real<T>(min, max));
  }

  static constexpr T minimum_length_squared = std::numeric_limits<T>::min();

  // [[nodiscard]] static vec3<T> random_unit_vector() {
  //   while (true) {
  //     vec3<T> p = random(-1, 1);
  //     auto lensq = p.squared_norm();
  //     if (minimum_length_squared < lensq && lensq <= 1) {
  //       return p / std::sqrt(lensq);
  //     }
  //   }
  // }

  // Utilizes Marsaglia’s sphere method
  [[nodiscard]] static vec3<T> random_unit_vector() {
    const T z = random_real<T>(-1, 1);
    const T phi = random_real<T>(0, 2 * std::numbers::pi_v<T>);
    const T r = std::sqrt(T{1} - z * z);
    return {r * std::cos(phi), r * std::sin(phi), z};
  }

  [[nodiscard]] static vec3<T> random_on_hemisphere(const vec3<T> &normal) {
    auto unit = random_unit_vector();

    if (dot(unit, normal) > T{0})
      return unit;
    else
      return -unit;
  }

  [[nodiscard]] static vec3<T> reflect(const vec3<T> &v,
                                       const vec3<T> &normal) {
    return v - 2 * dot(v, normal) * normal;
  }

  [[nodiscard]] bool near_zero() const noexcept {
    constexpr T threshold = T{100} * std::numeric_limits<T>::epsilon();

    return std::abs(e[0]) < threshold && std::abs(e[1]) < threshold &&
           std::abs(e[2]) < threshold;
  }

  // friend function definitions

  friend std::ostream &operator<<(std::ostream &out, const vec3<T> &v) {
    return out << '{' << v.e[0] << ", " << v.e[1] << ", " << v.e[2] << '}';
  }

  [[nodiscard]] friend constexpr auto operator+(const vec3<T> &u,
                                                const vec3<T> &v) noexcept {
    return vec3(u.e[0] + v.e[0], u.e[1] + v.e[1], u.e[2] + v.e[2]);
  }

  [[nodiscard]] friend constexpr auto operator+(T t,
                                                const vec3<T> &v) noexcept {
    return vec3(t + v.e[0], t + v.e[1], t + v.e[2]);
  }

  [[nodiscard]] friend constexpr auto operator+(const vec3<T> &v,
                                                T t) noexcept {
    return t + v;
  }

  [[nodiscard]] friend constexpr auto operator-(const vec3<T> &u,
                                                const vec3<T> &v) noexcept {
    return vec3(u.e[0] - v.e[0], u.e[1] - v.e[1], u.e[2] - v.e[2]);
  }

  [[nodiscard]] friend constexpr auto operator-(T t,
                                                const vec3<T> &v) noexcept {
    return vec3(t - v.e[0], t - v.e[1], t - v.e[2]);
  }

  [[nodiscard]] friend constexpr auto operator-(const vec3<T> &v,
                                                T t) noexcept {
    return vec3(v.e[0] - t, v.e[1] - t, v.e[2] - t);
  }

  [[nodiscard]] friend constexpr auto operator*(const vec3<T> &u,
                                                const vec3<T> &v) noexcept {
    return vec3(u.e[0] * v.e[0], u.e[1] * v.e[1], u.e[2] * v.e[2]);
  }

  [[nodiscard]] friend constexpr auto operator*(T t,
                                                const vec3<T> &v) noexcept {
    return vec3(t * v.e[0], t * v.e[1], t * v.e[2]);
  }

  [[nodiscard]] friend constexpr auto operator*(const vec3<T> &v,
                                                T t) noexcept {
    return t * v;
  }

  [[nodiscard]] friend constexpr auto operator/(const vec3<T> &v,
                                                T t) noexcept {
    assert(t != T{0});
    return (1 / t) * v;
  }

  [[nodiscard]] friend constexpr auto dot(const vec3<T> &u,
                                          const vec3<T> &v) noexcept {
    return u.e[0] * v.e[0] + u.e[1] * v.e[1] + u.e[2] * v.e[2];
  }

  [[nodiscard]] friend constexpr auto cross(const vec3<T> &u,
                                            const vec3<T> &v) noexcept {
    return vec3(u.e[1] * v.e[2] - u.e[2] * v.e[1],
                u.e[2] * v.e[0] - u.e[0] * v.e[2],
                u.e[0] * v.e[1] - u.e[1] * v.e[0]);
  }

  [[nodiscard]] friend auto unit_vector(const vec3<T> &v) noexcept {
    auto len = v.length();
    assert(len != T{0});
    return v / len;
  }
};
