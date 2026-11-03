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
#include <mesh_generators/quad.h>
#include <vec3.h>

[[nodiscard]] inline std::tuple<Camera<double>, std::vector<Mesh<double>>>
create_dragon_mirror_scene(const std::filesystem::path &project_root = ".") {
  auto [camera, meshes] = create_real_scene(project_root);
  using Point = point3<double>;
  using Vector = vec3<double>;

  const auto mirror = []() {
    return std::make_unique<Metal<double>>(Color<double>(0.96, 0.96, 0.96));
  };

  // Keep the existing grounded dragon and replace the glass slab with a mirror.
  // The floor extends well beyond the room and the visible foreground.
  meshes.back() =
      make_quad<double>(Point(-100.0, 0.0, 100.0), Vector(200.0, 0.0, 0.0),
                        Vector(0.0, 0.0, -200.0), mirror());
  meshes.reserve(10);

  // Three inward-facing mirrors. The front (+Z) and ceiling remain open so
  // the camera can see into the room and the sky can illuminate the dragon.
  meshes.push_back(make_quad<double>(Point(-4.0, 0.0, -6.0),
                                     Vector(8.0, 0.0, 0.0),
                                     Vector(0.0, 7.0, 0.0), mirror()));
  meshes.push_back(make_quad<double>(Point(-4.0, 0.0, 8.0),
                                     Vector(0.0, 0.0, -14.0),
                                     Vector(0.0, 7.0, 0.0), mirror()));
  meshes.push_back(make_quad<double>(Point(4.0, 0.0, -6.0),
                                     Vector(0.0, 0.0, 14.0),
                                     Vector(0.0, 7.0, 0.0), mirror()));

  const auto seam = [&meshes](const Point &minimum, const Point &maximum,
                              const Color<double> &color) {
    meshes.push_back(make_aligned_box<double>(
        minimum, maximum, std::make_unique<Lambertian<double>>(color)));
  };

  const Color<double> left_color(0.80, 0.12, 0.08);
  const Color<double> right_color(0.08, 0.25, 0.80);
  const Color<double> back_color(0.85, 0.65, 0.08);

  // Inset the matte trim slightly from the mirror planes to avoid coplanar
  // intersections. Each side keeps the same color along both of its seams.
  seam(Point(-3.998, 0.002, -5.9), Point(-3.9, 0.1, 8.0), left_color);
  seam(Point(3.9, 0.002, -5.9), Point(3.998, 0.1, 8.0), right_color);
  seam(Point(-3.998, 0.002, -5.998), Point(3.998, 0.1, -5.9), back_color);
  seam(Point(-3.998, 0.1, -5.998), Point(-3.9, 7.0, -5.9), left_color);
  seam(Point(3.9, 0.1, -5.998), Point(3.998, 7.0, -5.9), right_color);

  camera.look_from = Point(0.0, 4.0, 11.0);
  camera.look_at = Point(0.0, 1.5, 0.0);
  camera.vfov = 40;
  // Allow repeated reflections between the walls before terminating a path.
  camera.max_depth = 24;

  return std::make_tuple(std::move(camera), std::move(meshes));
}
