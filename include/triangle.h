#pragma once

#include <cmath>
#include <concepts>
#include <memory>
#include <optional>
#include <utility>

#include <hittable.h>
#include <material.h>

template <std::floating_point T> struct TriangleIntersection {
  // barycentric coordinates
  T u;
  T v;
  T t;
};

template <std::floating_point T> class Triangle : public Hittable<T> {

public:
  Triangle(const point3<T> &v0, const point3<T> &v1, const point3<T> &v2,
           std::unique_ptr<Material<T>> surface_material) noexcept
      : v0(v0), v1(v1), v2(v2), material(std::move(surface_material)) {
    assert(material != nullptr);

    e1 = v1 - v0;
    e2 = v2 - v0;
    normal = cross(e1, e2);
    _bounding_box =
        BoundingBox<T>(BoundingBox<T>(v0, v1), BoundingBox<T>(v2, v2));
  }

  [[nodiscard]] BoundingBox<T> bounding_box() const override {
    return _bounding_box;
  }

  [[nodiscard]] std::optional<HitRecord<T>>
  hit(const Ray<T> &ray, Interval<T> interval) const override {

    if (auto intersection = intersect_triangle(ray)) {
      // make sure the intersection point lies inside the triangle (including
      // its edges) uses barycentric coords (both coords have to be larger than
      // 0 and sum to less than 1)
      if (!(intersection->u >= T{0} && intersection->v >= T{0} &&
            intersection->u + intersection->v <= T{1})) {
        return std::nullopt;
      }

      // Reject hits outside the permitted ray range.
      if (!interval.surrounds(intersection->t))
        return std::nullopt;

      const auto outward_normal = unit_vector(normal);

      return HitRecord<T>(ray.at(intersection->t), intersection->t, *material,
                          ray, outward_normal);
    }

    return std::nullopt;
  }

private:
  point3<T> v0;
  point3<T> v1;
  point3<T> v2;
  std::unique_ptr<Material<T>> material;
  BoundingBox<T> _bounding_box;

  vec3<T> e1;
  vec3<T> e2;
  vec3<T> normal;

  [[nodiscard]] std::optional<TriangleIntersection<T>>
  intersect_triangle(const Ray<T> &r) const {

    const vec3<T> direction = r.direction();
    const auto origin = r.origin();

    const auto S = origin - v0;

    const auto w = cross(direction, S);

    const auto determinant = dot(-direction, normal);

    constexpr T epsilon = T{1e-8};
    if (std::abs(determinant) <= epsilon)
      return std::nullopt;

    const auto t_num = dot(S, normal);
    const auto u_num = dot(-e2, w);
    const auto v_num = dot(e1, w);

    const auto inverse_det = T{1} / determinant;

    const auto intersection = TriangleIntersection<T>{
        u_num * inverse_det,
        v_num * inverse_det,
        t_num * inverse_det,
    };

    return intersection;
  }
};
