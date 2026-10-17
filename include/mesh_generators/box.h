#pragma once

#include <cassert>
#include <concepts>
#include <memory>
#include <utility>
#include <vector>

#include <mesh.h>

[[nodiscard]] inline std::vector<TriangleIndices> box_triangle_indices() {
  return std::vector<TriangleIndices>{
      {0, 2, 1}, {2, 3, 1}, // -z
      {4, 5, 6}, {5, 7, 6}, // +z
      {0, 1, 4}, {1, 5, 4}, // -y
      {2, 6, 3}, {6, 7, 3}, // +y
      {0, 4, 2}, {4, 6, 2}, // -x
      {1, 3, 5}, {3, 7, 5}, // +x
  };
}

template <std::floating_point T>
[[nodiscard]] Mesh<T>
make_aligned_box(const point3<T> &min_corner, const point3<T> &max_corner,
                 std::unique_ptr<Material<T>> surface_material) {
  assert(surface_material != nullptr);
  assert(min_corner[0] < max_corner[0] && min_corner[1] < max_corner[1] &&
         min_corner[2] < max_corner[2]);

  const T x0 = min_corner[0], y0 = min_corner[1], z0 = min_corner[2];
  const T x1 = max_corner[0], y1 = max_corner[1], z1 = max_corner[2];

  std::vector<point3<T>> positions = {
      point3<T>(x0, y0, z0), // 0: min                  (min)
      point3<T>(x1, y0, z0), // 1: min + u              (max x)
      point3<T>(x0, y1, z0), // 2: min + v              (max y)
      point3<T>(x1, y1, z0), // 3: min + u + v          (max x, y)
      point3<T>(x0, y0, z1), // 4: min + w              (max z)
      point3<T>(x1, y0, z1), // 5: min + u + w          (max x, z)
      point3<T>(x0, y1, z1), // 6: min + v + w          (max y, z)
      point3<T>(x1, y1, z1), // 7: max                  (max)
  };

  return Mesh<T>{std::move(positions),
                 {},
                 box_triangle_indices(),
                 std::move(surface_material)};
}

template <std::floating_point T>
[[nodiscard]] Mesh<T> make_box(const point3<T> origin, const vec3<T> edge_u,
                               const vec3<T> edge_v, const vec3<T> edge_w,
                               std::unique_ptr<Material<T>> surface_material) {
  // The edges must form a right-handed set, or every face comes out
  // inside-out.
  assert(dot(cross(edge_u, edge_v), edge_w) > 0);
  assert(surface_material != nullptr);

  std::vector<point3<T>> positions = {
      origin,                            // 0
      origin + edge_u,                   // 1
      origin + edge_v,                   // 2
      origin + edge_u + edge_v,          // 3
      origin + edge_w,                   // 4
      origin + edge_u + edge_w,          // 5
      origin + edge_v + edge_w,          // 6
      origin + edge_u + edge_v + edge_w, // 7
  };
  return Mesh<T>{std::move(positions),
                 {},
                 box_triangle_indices(),
                 std::move(surface_material)};
}
