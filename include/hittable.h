#pragma once

#include <interval.h>
#include <ray.h>

#include <cassert>
#include <cmath>
#include <concepts>
#include <functional>
#include <limits>
#include <optional>

// forward declaration to avoid circular imports
template <std::floating_point T> class Material;

template <std::floating_point T> class HitRecord {
public:
  point3<T> p;
  vec3<T> normal;
  T t;
  std::reference_wrapper<const Material<T>> material;
  bool front_face;

  HitRecord(point3<T> hit_point, T ray_parameter,
            const Material<T> &surface_material)
      : p(hit_point), t(ray_parameter), material(surface_material) {}

  HitRecord(point3<T> hit_point, T ray_parameter,
            const Material<T> &surface_material, const Ray<T> &incoming_ray,
            const vec3<T> &outward_normal)
      : p(hit_point), t(ray_parameter), material(surface_material) {
    set_face_normal(incoming_ray, outward_normal);
  }

  void set_face_normal(const Ray<T> &r,
                       const vec3<T> &outward_normal) noexcept {

    constexpr T tolerance = std::sqrt(std::numeric_limits<T>::epsilon());
    assert(std::abs(outward_normal.squared_norm() - T{1}) <= tolerance);

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
