#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <optional>

#include <color.h>
#include <interactions.h>
#include <ray.h>
#include <utility.h>
#include <vec3.h>

template <std::floating_point T> struct ScatterRecord {
  Ray<T> scattered;
  Color<T> attenuation;
};

template <std::floating_point T> class Material {
public:
  virtual ~Material() = default;

  virtual std::optional<ScatterRecord<T>>
  scatter(const Ray<T> &ray_in, const SurfaceInteraction<T> &hit) const = 0;
};

template <std::floating_point T> class Lambertian : public Material<T> {
private:
  Color<T> albedo;

public:
  constexpr Lambertian(const Color<T> &albedo) noexcept : albedo(albedo) {}

  std::optional<ScatterRecord<T>>
  scatter(const Ray<T> &ray_in,
          const SurfaceInteraction<T> &hit) const override {
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

template <std::floating_point T> class Dielectric : public Material<T> {
private:
  T refractive_index;

public:
  explicit Dielectric(T index) noexcept : refractive_index(index) {
    assert(std::isfinite(index) && index > T{0});
  }

  std::optional<ScatterRecord<T>>
  scatter(const Ray<T> &ray_in,
          const SurfaceInteraction<T> &hit) const override {
    const auto incoming = unit_vector(ray_in.direction());
    const auto &normal = hit.geometric_normal;
    const T eta = hit.front_face ? T{1} / refractive_index : refractive_index;
    const T cos_theta = std::clamp(dot(-incoming, normal), T{0}, T{1});
    const T sin_theta = std::sqrt(std::max(T{0}, T{1} - cos_theta * cos_theta));

    const T r0_base = (T{1} - eta) / (T{1} + eta);
    const T r0 = r0_base * r0_base;
    const T one_minus_cos = T{1} - cos_theta;
    const T reflectance = r0 + (T{1} - r0) * one_minus_cos * one_minus_cos *
                                   one_minus_cos * one_minus_cos *
                                   one_minus_cos;

    vec3<T> direction;
    if (eta * sin_theta > T{1} || random_real<T>() < reflectance) {
      direction = vec3<T>::reflect(incoming, normal);
    } else {
      const auto perpendicular = eta * (incoming + cos_theta * normal);
      const auto parallel =
          -std::sqrt(std::max(T{0}, T{1} - perpendicular.squared_norm())) *
          normal;
      direction = perpendicular + parallel;
    }

    return ScatterRecord<T>(Ray<T>(hit.p, direction, ray_in.time()),
                            Color<T>(1, 1, 1));
  }
};

template <std::floating_point T> class Metal : public Material<T> {
private:
  Color<T> albedo;

public:
  constexpr Metal(const Color<T> &albedo) noexcept : albedo(albedo) {}

  std::optional<ScatterRecord<T>>
  scatter(const Ray<T> &ray_in,
          const SurfaceInteraction<T> &hit) const override {
    vec3<T> reflection =
        vec3<T>::reflect(ray_in.direction(), hit.shading_normal);

    if (dot(reflection, hit.geometric_normal) <= T{0})
      return std::nullopt;

    auto scattered = Ray(hit.p, reflection, ray_in.time());
    auto attenuation = albedo;

    return ScatterRecord<T>(scattered, attenuation);
  }
};
