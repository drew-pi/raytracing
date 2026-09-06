#pragma once

#include <cassert>
#include <cmath>
#include <concepts>
#include <hittable.h>
#include <optional>
#include <ray.h>

template <std::floating_point T> class Sphere : public Hittable<T> {
private:
  point3<T> center;
  T radius;

public:
  constexpr Sphere(const point3<T> &center, T radius) noexcept
      : center(center), radius(radius) {
    assert(radius > T{0});
  }

  [[nodiscard]] std::optional<HitRecord<T>>
  hit(const Ray<T> &r, Interval<T> interval) const override {
    auto oc = center - r.origin();

    // make sure we are never dividing by zero
    auto a = dot(r.direction(), r.direction());
    assert(a != T{0});

    auto h = dot(r.direction(), oc);
    auto c = dot(oc, oc) - radius * radius;

    auto discriminant = h * h - a * c;

    if (discriminant < 0) {
      return std::nullopt;
    }

    auto sqrtd = std::sqrt(discriminant);

    // Find the nearest root that lies in the acceptable range.
    auto root = (h - sqrtd) / a;
    if (!interval.surrounds(root)) {
      root = (h + sqrtd) / a;
      if (!interval.surrounds(root))
        return std::nullopt;
    }

    auto record = HitRecord<T>();
    record.t = root;
    record.p = r.at(root);
    auto outward_normal = (record.p - center) / radius;
    // assigns record.normal and record.front_face
    record.set_face_normal(r, outward_normal);

    return std::optional<HitRecord<T>>(record);
  }
};
