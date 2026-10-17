#pragma once

#include <cmath>
#include <concepts>
#include <optional>
#include <span>

#include <bound_box.h>
#include <interactions.h>
#include <interval.h>
#include <mesh.h>
#include <ray.h>
#include <vec3.h>

template <std::floating_point T>
[[nodiscard]] std::optional<TriangleIntersectionRecord<T>>
intersect_triangle(const Ray<T> &ray, Interval<T> interval, const point3<T> &v0,
                   const point3<T> &v1, const point3<T> &v2) {

  const vec3<T> direction = ray.direction();
  const auto origin = ray.origin();

  const auto e1 = v1 - v0;
  const auto e2 = v2 - v0;
  const auto normal = cross(e1, e2);
  const auto s = origin - v0;

  const auto w = cross(direction, s);

  const auto determinant = dot(-direction, normal);

  constexpr T epsilon = T{1e-8};
  if (std::abs(determinant) <= epsilon)
    return std::nullopt;

  const auto t_num = dot(s, normal);
  const auto u_num = dot(-e2, w);
  const auto v_num = dot(e1, w);

  const auto inverse_det = T{1} / determinant;

  const auto u = u_num * inverse_det;
  const auto v = v_num * inverse_det;
  const auto t = t_num * inverse_det;

  // make sure the intersection point lies inside the triangle (including
  // its edges) uses barycentric coords (both coords have to be larger than
  // 0 and sum to less than 1)
  if (!(u >= T{0} && v >= T{0} && u + v <= T{1})) {
    return std::nullopt;
  }

  // Reject hits outside the permitted ray range.
  if (!interval.surrounds(t))
    return std::nullopt;

  return TriangleIntersectionRecord<T>(t, u, v);
}

template <std::floating_point T>
[[nodiscard]] BoundingBox<T> triangle_bounding_box(const point3<T> &v0,
                                                   const point3<T> &v1,
                                                   const point3<T> &v2) {
  return BoundingBox<T>(v0, v1, v2);
}

template <std::floating_point T>
[[nodiscard]] BoundingBox<T>
triangle_bounding_box(TriangleIndices triangle,
                      std::span<const point3<T>> positions) {
  const auto &v0 = positions[triangle.v0];
  const auto &v1 = positions[triangle.v1];
  const auto &v2 = positions[triangle.v2];

  return BoundingBox<T>(v0, v1, v2);
}

template <std::floating_point T>
[[nodiscard]] constexpr point3<T>
triangle_centroid(const point3<T> &p1, const point3<T> &p2,
                  const point3<T> &p3) noexcept {
  constexpr T one_third = T{1} / T{3}; // computed at compile time
  return point3<T>((p1.x() + p2.x() + p3.x()) * one_third,
                   (p1.y() + p2.y() + p3.y()) * one_third,
                   (p1.z() + p2.z() + p3.z()) * one_third);
}

template <std::floating_point T>
[[nodiscard]] point3<T>
triangle_centroid(TriangleIndices triangle,
                  std::span<const point3<T>> positions) noexcept {
  constexpr T one_third = T{1} / T{3}; // computed at compile time
  const auto &v0 = positions[triangle.v0];
  const auto &v1 = positions[triangle.v1];
  const auto &v2 = positions[triangle.v2];

  return (v0 + v1 + v2) * one_third;
}
