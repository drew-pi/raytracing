#pragma once

#include <filesystem>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "real_scene.h"

#include <camera.h>
#include <color.h>
#include <material.h>
#include <mesh.h>
#include <mesh_generators/box.h>
#include <vec3.h>

[[nodiscard]] inline std::tuple<Camera<double>, std::vector<Mesh<double>>>
create_dragon_cage_scene(const std::filesystem::path &project_root = ".") {
  // Retain the existing forward-facing dragon and the solid glass floor.
  auto [camera, meshes] = create_real_scene(project_root);
  using Point = point3<double>;

  constexpr double half_width = 2.8;
  constexpr double half_depth = 3.7;
  constexpr double height = 3.7;
  constexpr double half_bar = 0.035;
  constexpr double rail_thickness = 0.10;
  constexpr int width_intervals = 10;
  constexpr int depth_intervals = 14;
  meshes.reserve(69);

  const auto bar = [&meshes](const Point &minimum, const Point &maximum) {
    meshes.push_back(make_aligned_box<double>(
        minimum, maximum,
        std::make_unique<Metal<double>>(Color<double>(0.42, 0.45, 0.49))));
  };

  // Four corner posts sit on y = 0, outside the dragon's bounds.
  for (const double x : {-half_width, half_width}) {
    for (const double z : {-half_depth, half_depth}) {
      bar(Point(x - half_bar, 0.0, z - half_bar),
          Point(x + half_bar, height, z + half_bar));
    }
  }

  // Upper and lower rectangular frames. Rails end at the corner posts rather
  // than duplicating them; vertical bars fill the spaces between these rails.
  for (const double y : {0.0, height - rail_thickness}) {
    for (const double z : {-half_depth, half_depth}) {
      bar(Point(-half_width + half_bar, y, z - half_bar),
          Point(half_width - half_bar, y + rail_thickness, z + half_bar));
    }
    for (const double x : {-half_width, half_width}) {
      bar(Point(x - half_bar, y, -half_depth + half_bar),
          Point(x + half_bar, y + rail_thickness, half_depth - half_bar));
    }
  }

  // Front and back bars are offset by half a spacing so no bar runs directly
  // through the center of the dragon's face in a frontal view.
  for (int i = 0; i < width_intervals; ++i) {
    const double x =
        -half_width + (i + 0.5) * (2.0 * half_width / width_intervals);
    for (const double z : {-half_depth, half_depth}) {
      bar(Point(x - half_bar, rail_thickness, z - half_bar),
          Point(x + half_bar, height - rail_thickness, z + half_bar));
    }
  }

  // Vertical bars along both sides, excluding the existing corner posts.
  for (int i = 1; i < depth_intervals; ++i) {
    const double z = -half_depth + i * (2.0 * half_depth / depth_intervals);
    for (const double x : {-half_width, half_width}) {
      bar(Point(x - half_bar, rail_thickness, z - half_bar),
          Point(x + half_bar, height - rail_thickness, z + half_bar));
    }
  }

  // A barred roof closes the cage without blocking the sky completely.
  for (int i = 1; i < width_intervals; ++i) {
    const double x = -half_width + i * (2.0 * half_width / width_intervals);
    bar(Point(x - half_bar, height - rail_thickness, -half_depth + half_bar),
        Point(x + half_bar, height, half_depth - half_bar));
  }

  camera.look_from = Point(7.0, 5.0, 12.0);
  camera.look_at = Point(0.0, 1.6, 0.0);
  camera.vfov = 40;
  camera.max_depth = 16;

  return std::make_tuple(std::move(camera), std::move(meshes));
}
