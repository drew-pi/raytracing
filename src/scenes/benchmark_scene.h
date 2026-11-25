#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include <camera.h>
#include <color.h>
#include <material.h>
#include <mesh.h>
#include <mesh_generators/box.h>
#include <mesh_generators/ply.h>
#include <vec3.h>

using BenchmarkScalar = float;

// Pass this to render_multi_thread() or render_checksum(); thread count is a
// render-call parameter, not a Camera or Scene setting.
inline constexpr int benchmark_thread_count = 1;
inline constexpr char benchmark_scene_id[] = "xyzrgb_dragon_glass_f32_v1";
inline constexpr char benchmark_scene_source[] = "src/scenes/benchmark_scene.h";

[[nodiscard]] inline Camera<BenchmarkScalar> create_benchmark_camera() {
  Camera<BenchmarkScalar> camera;
  camera.aspect_ratio = 4.0 / 3.0;
  camera.image_width = 1600;
  camera.samples_per_pixel = 200;
  camera.max_depth = 20;
  camera.vfov = 40;
  camera.look_from = point3<BenchmarkScalar>(0.0, 4.0, 11.0);
  camera.look_at = point3<BenchmarkScalar>(0.0, 1.5, 0.0);
  camera.v_up = vec3<BenchmarkScalar>(0.0, 1.0, 0.0);
  return camera;
}

inline void write_benchmark_metadata(std::ostream &out) {
  const auto camera = create_benchmark_camera();
  out << "{\"scene\":\"" << benchmark_scene_id << "\",\"scene_source\":\""
      << benchmark_scene_source << "\",\"width\":" << camera.image_width
      << ",\"spp\":" << camera.samples_per_pixel
      << ",\"max_depth\":" << camera.max_depth << "}\n";
}

// Independent snapshot of the real scene so future scene edits do not change
// the benchmark workload. project_root is the directory containing assets/.
[[nodiscard]] inline std::tuple<Camera<BenchmarkScalar>,
                                std::vector<Mesh<BenchmarkScalar>>>
create_benchmark_scene(const std::filesystem::path &project_root = ".") {
  using Point = point3<BenchmarkScalar>;
  using Colour = Color<BenchmarkScalar>;

  // Imported meshes have private buffers and no transform API. Rebuild through
  // the public constructor after centering and applying a uniform positive
  // scale.
  const auto place_mesh = [](Mesh<BenchmarkScalar> mesh, const Point &center,
                             BenchmarkScalar longest_extent,
                             bool stand_on_floor = false,
                             bool face_forward = false) {
    std::vector<Point> positions;
    positions.reserve(mesh.vertex_count());
    mesh.append_positions(positions);
    if (positions.empty()) {
      throw std::runtime_error("Benchmark scene: cannot place an empty mesh");
    }

    // The dragon faces source +X; rotate -90 degrees about Y to face camera +Z.
    if (face_forward) {
      for (auto &position : positions) {
        position = Point(-position[2], position[1], position[0]);
      }
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
    const BenchmarkScalar source_extent =
        std::max({extent[0], extent[1], extent[2]});
    if (!std::isfinite(source_extent) || source_extent <= 0.0) {
      throw std::runtime_error("Benchmark scene: mesh has invalid bounds");
    }
    Point source_center = 0.5 * minimum + 0.5 * maximum;
    // Anchor the lowest vertex to center.y() when placing a grounded model.
    if (stand_on_floor) {
      source_center[1] = minimum[1];
    }
    const BenchmarkScalar scale = longest_extent / source_extent;
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
    return Mesh<BenchmarkScalar>{std::move(positions),
                                 {},
                                 std::move(triangles),
                                 std::move(mesh).move_material()};
  };

  std::vector<Mesh<BenchmarkScalar>> meshes;
  meshes.reserve(2);

  meshes.push_back(place_mesh(
      load_ply<BenchmarkScalar>(project_root / "assets/xyzrgb_dragon.ply",
                                std::make_unique<Lambertian<BenchmarkScalar>>(
                                    Colour(0.24, 0.55, 0.38))),
      Point(0.0, 0.0, 0.0), 6.0, true, true));

  // A closed slab supplies both entry and exit boundaries for glass refraction.
  // Its upper face is y = 0, exactly where the dragon's lowest vertex rests.
  meshes.push_back(make_aligned_box<BenchmarkScalar>(
      Point(-12.0, -0.25, -12.0), Point(12.0, 0.0, 12.0),
      std::make_unique<Dielectric<BenchmarkScalar>>(1.5)));

  auto camera = create_benchmark_camera();

  return std::make_tuple(std::move(camera), std::move(meshes));
}
