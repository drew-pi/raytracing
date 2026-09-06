#pragma once

#include <interval.h>
#include <ray.h>

#include <concepts>
#include <optional>

template <std::floating_point T> class HitRecord {
public:
  point3<T> p;
  vec3<T> normal;
  T t;
  bool front_face;

  constexpr void set_face_normal(const Ray<T> &r,
                                 const vec3<T> &outward_normal) noexcept {

    auto epsilon = 0.01;
    assert(std::abs(outward_normal.length() - 1) < epsilon);

    front_face = dot(r.direction(), outward_normal) < 0;
    normal = front_face ? outward_normal : -outward_normal;
  }
};

template <std::floating_point T> class Hittable {
public:
  // define the destructor
  virtual ~Hittable() = default;

  [[nodiscard]] virtual std::optional<HitRecord<T>>
  hit(const Ray<T> &r, Interval<T> interval) const = 0;
};
