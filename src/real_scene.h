#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include <camera.h>
#include <color.h>
#include <material.h>
#include <mesh.h>
#include <mesh_generators/ply.h>
#include <mesh_generators/quad.h>
#include <vec3.h>

// project_root is the directory containing assets/, not the assets/ directory.
[[nodiscard]] inline std::tuple<Camera<double>, std::vector<Mesh<double>>>
create_real_scene(const std::filesystem::path &project_root = ".") {
  using Point = point3<double>;
  using Colour = Color<double>;

  // Imported meshes have private buffers and no transform API. Rebuild through
  // the public constructor after centering and applying a uniform positive
  // scale.
  const auto place_mesh = [](Mesh<double> mesh, const Point &center,
                             double longest_extent,
                             bool stand_on_floor = false) {
    std::vector<Point> positions;
    positions.reserve(mesh.vertex_count());
    mesh.append_positions(positions);
    if (positions.empty()) {
      throw std::runtime_error("Real scene: cannot place an empty mesh");
    }

    Point minimum = positions.front();
    Point maximum = positions.front();
    for (const auto &position : positions) {
      for (int axis = 0; axis < 3; ++axis) {
        minimum[axis] = std::min(minimum[axis], position[axis]);
        maximum[axis] = std::max(maximum[axis], position[axis]);
      }
    }

    const auto extent = maximum - minimum;
    const double source_extent = std::max({extent[0], extent[1], extent[2]});
    if (!std::isfinite(source_extent) || source_extent <= 0.0) {
      throw std::runtime_error("Real scene: mesh has invalid bounds");
    }
    Point source_center = 0.5 * minimum + 0.5 * maximum;
    // Anchor the lowest vertex to center.y() when placing a grounded model.
    if (stand_on_floor) {
      source_center[1] = minimum[1];
    }
    const double scale = longest_extent / source_extent;
    for (auto &position : positions) {
      position = center + scale * (position - source_center);
    }

    std::vector<SceneTriangle> source_triangles;
    source_triangles.reserve(mesh.triangle_count());
    mesh.append_triangles(source_triangles, 0, 0);

    std::vector<TriangleIndices> triangles;
    triangles.reserve(source_triangles.size());
    for (const auto &triangle : source_triangles) {
      triangles.push_back(triangle.vertices);
    }

    // load_ply currently supplies no vertex normals, so retain flat shading.
    return Mesh<double>{std::move(positions),
                        {},
                        std::move(triangles),
                        std::move(mesh).move_material()};
  };

  std::vector<Mesh<double>> meshes;
  meshes.reserve(2);

  // meshes.push_back(place_mesh(
  //     load_ply<double>(
  //         project_root / "assets/bunny/reconstruction/bun_zipper.ply",
  //         std::make_unique<Lambertian<double>>(Colour(0.72, 0.36, 0.20))),
  //     Point(-1.6, 0.0, 0.0), 2.4));

  // meshes.push_back(place_mesh(
  //     load_ply<double>(
  //         project_root / "assets/dragon_recon/dragon_vrip.ply",
  //         std::make_unique<Lambertian<double>>(Colour(0.24, 0.55, 0.38))),
  //     Point(1.6, 0.0, 0.0), 2.4));

  meshes.push_back(
      place_mesh(load_ply<double>(project_root / "assets/xyzrgb_dragon.ply",
                                  std::make_unique<Lambertian<double>>(
                                      Colour(0.24, 0.55, 0.38))),
                 Point(0.0, 0.0, 0.0), 6.0, true));

  // Counterclockwise winding gives the mirror an upward-facing normal.
  meshes.push_back(make_quad<double>(
      Point(-100.0, 0.0, 100.0), vec3<double>(200.0, 0.0, 0.0),
      vec3<double>(0.0, 0.0, -200.0),
      std::make_unique<Metal<double>>(Colour(0.90, 0.90, 0.90))));

  Camera<double> camera;
  camera.aspect_ratio = 4.0 / 3.0;
  camera.image_width = 2400;
  camera.samples_per_pixel = 768;
  camera.max_depth = 12;
  camera.vfov = 40;
  camera.look_from = Point(0.0, 4.0, 11.0);
  camera.look_at = Point(0.0, 1.5, 0.0);
  camera.v_up = vec3<double>(0.0, 1.0, 0.0);

  return std::make_tuple(std::move(camera), std::move(meshes));
}
