#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <ostream>

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

  // friend function definitions

  friend std::ostream &operator<<(std::ostream &out, const vec3<T> &v) {
    return out << '{' << v.e[0] << ", " << v.e[1] << ", " << v.e[2] << '}';
  }

  [[nodiscard]] friend constexpr auto operator+(const vec3<T> &u,
                                                const vec3<T> &v) noexcept {
    return vec3(u.e[0] + v.e[0], u.e[1] + v.e[1], u.e[2] + v.e[2]);
  }

  [[nodiscard]] friend constexpr auto operator-(const vec3<T> &u,
                                                const vec3<T> &v) noexcept {
    return vec3(u.e[0] - v.e[0], u.e[1] - v.e[1], u.e[2] - v.e[2]);
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
