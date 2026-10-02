#pragma once

#include <cassert>
#include <cmath>
#include <concepts>
#include <memory>
#include <optional>
#include <utility>

#include <hittable.h>
#include <material.h>
#include <ray.h>

template <std::floating_point T> class Sphere : public Hittable<T> {
private:
  point3<T> center;
  T radius;
  std::unique_ptr<Material<T>> material;

public:
  Sphere(const point3<T> &center, T radius,
         std::unique_ptr<Material<T>> surface_material) noexcept
      : center(center), radius(radius), material(std::move(surface_material)) {
    // TODO: Initialize the material pointer `material`.
    assert(radius > T{0});
    assert(material != nullptr);
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

    auto ray_parameter = root;
    auto hit_point = r.at(root);
    auto outward_normal = (hit_point - center) / radius;
    // material is guaranteed to be non null because of the assertion in the
    // constructor (at least for debug runs)
    auto record =
        HitRecord<T>(hit_point, ray_parameter, *material, r, outward_normal);

    return std::optional<HitRecord<T>>(record);
  }
};
