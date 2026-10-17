#pragma once

#include <cmath>
#include <memory>
#include <numbers>
#include <tuple>

#include <camera.h>
#include <hittable_list.h>
#include <material.h>
#include <sphere.h>
#include <vec3.h>

using BenchmarkSetup = std::tuple<Camera<double>, HittableList<double>>;

[[nodiscard]] inline BenchmarkSetup create_scene() {

  HittableList<double> world;

  auto diffuse = [&](point3<double> center, double radius,
                     Color<double> color) {
    world.add(std::make_unique<Sphere<double>>(
        center, radius, std::make_unique<Lambertian<double>>(color)));
  };

  auto metal = [&](point3<double> center, double radius, Color<double> color) {
    world.add(std::make_unique<Sphere<double>>(
        center, radius, std::make_unique<Metal<double>>(color)));
  };

  // Dark ground and a field of small, alternating colored spheres.
  diffuse(point3<double>(0, -1000, 0), 1000, Color<double>(0.12, 0.16, 0.23));

  for (int x = -5; x <= 5; ++x) {
    for (int z = -10; z <= 0; ++z) {
      const bool accent = (x + z + 100) % 4 == 0;
      const auto color = accent ? Color<double>(0.12, 0.55, 0.68)
                                : Color<double>(0.35, 0.23, 0.43);

      diffuse(point3<double>(x * 0.9, 0.11, z * 0.9), 0.11, color);
    }
  }

  // The central mirrored sphere.
  metal(point3<double>(0, 2.7, -5.5), 1.1, Color<double>(0.94, 0.96, 1.0));

  constexpr double pi = std::numbers::pi_v<double>;

  // An inner orbit of larger alternating gold and cyan spheres.
  for (int i = 0; i < 48; ++i) {
    const double angle = 2.0 * pi * i / 48.0;
    const point3<double> center(2.0 * std::cos(angle),
                                2.7 + 2.0 * std::sin(angle),
                                -5.5 + 0.25 * std::sin(3.0 * angle));

    if (i % 2 == 0) {
      metal(center, 0.15, Color<double>(0.95, 0.66, 0.24));
    } else {
      diffuse(center, 0.15, Color<double>(0.12, 0.72, 0.85));
    }
  }

  // A second, finer orbit, offset in depth from the first.
  for (int i = 0; i < 64; ++i) {
    const double angle = 2.0 * pi * (i + 0.5) / 64.0;
    const point3<double> center(2.75 * std::cos(angle),
                                3.1 + 2.75 * std::sin(angle),
                                -5.5 + 0.55 * std::cos(2.0 * angle));

    if (i % 4 == 0) {
      metal(center, 0.095, Color<double>(0.95, 0.82, 0.55));
    } else {
      diffuse(center, 0.095, Color<double>(0.65, 0.22, 0.55));
    }
  }

  // A distant outer orbit of small, alternating jewel-like spheres.
  for (int i = 0; i < 80; ++i) {
    const double angle = 2.0 * pi * (i + 0.25) / 80.0;
    const point3<double> center(3.35 * std::cos(angle),
                                3.55 + 3.35 * std::sin(angle),
                                -5.5 + 0.4 * std::sin(4.0 * angle));

    if (i % 5 == 0) {
      metal(center, 0.075, Color<double>(0.96, 0.78, 0.38));
    } else {
      diffuse(center, 0.075, Color<double>(0.32, 0.55, 0.88));
    }
  }

  // Larger foreground accents give the composition some depth.
  metal(point3<double>(-3.7, 0.45, -4.7), 0.45,
        Color<double>(0.87, 0.65, 0.31));
  metal(point3<double>(3.7, 0.45, -6.3), 0.45, Color<double>(0.58, 0.83, 0.94));

  Camera<double> camera;
  camera.aspect_ratio = 16.0 / 9.0;
  camera.image_width = 1600; // 1600 × 900 at 16:9
  camera.samples_per_pixel = 250;
  camera.max_depth = 20;
  camera.vfov = 38;
  camera.look_from = point3<double>(6, 4.3, 7);
  camera.look_at = point3<double>(0, 2.2, -5.5);
  camera.v_up = vec3<double>(0, 1, 0);

  return std::make_tuple(std::move(camera), std::move(world));
}
