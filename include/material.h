#pragma once

#include <concepts>
#include <optional>

#include <color.h>
#include <hittable.h>
#include <ray.h>

template <std::floating_point T> struct ScatterRecord {
  Ray<T> scattered;
  Color<T> attenuation;
};

template <std::floating_point T> class Material {
public:
  virtual ~Material() = default;

  virtual std::optional<ScatterRecord<T>>
  scatter(const Ray<T> &ray_in, const HitRecord<T> &hit) const = 0;
};

template <std::floating_point T> class Lambertian : public Material<T> {
private:
  Color<T> albedo;

public:
  constexpr Lambertian(const Color<T> &albedo) noexcept : albedo(albedo) {}

  std::optional<ScatterRecord<T>>
  scatter(const Ray<T> &ray_in, const HitRecord<T> &hit) const override {
    auto scatter_dir = hit.shading_normal + vec3<T>::random_unit_vector();

    // catch degenerate scatter direction if the random unit vector is almost
    // the exact opposite of the shading_normal at the hit point
    scatter_dir = scatter_dir.near_zero() ? hit.shading_normal : scatter_dir;

    if (dot(scatter_dir, hit.geometric_normal) <= T{0})
      return std::nullopt;

    auto scattered = Ray(hit.p, scatter_dir, ray_in.time());
    auto attenuation = albedo;

    return ScatterRecord<T>(scattered, attenuation);
  }
};

template <std::floating_point T> class Metal : public Material<T> {
private:
  Color<T> albedo;

public:
  constexpr Metal(const Color<T> &albedo) noexcept : albedo(albedo) {}

  std::optional<ScatterRecord<T>>
  scatter(const Ray<T> &ray_in, const HitRecord<T> &hit) const override {
    vec3<T> reflection =
        vec3<T>::reflect(ray_in.direction(), hit.shading_normal);

    if (dot(reflection, hit.geometric_normal) <= T{0})
      return std::nullopt;

    auto scattered = Ray(hit.p, reflection, ray_in.time());
    auto attenuation = albedo;

    return ScatterRecord<T>(scattered, attenuation);
  }
};
