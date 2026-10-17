#pragma once

#include <cassert>
#include <concepts>
#include <memory>
#include <utility>
#include <vector>

#include <mesh.h>

template <std::floating_point T>
[[nodiscard]] Mesh<T> make_quad(const point3<T> &origin, const vec3<T> &edge_u,
                                const vec3<T> &edge_v,
                                std::unique_ptr<Material<T>> surface_material) {

  assert(surface_material != nullptr);
  assert(cross(edge_u, edge_v).squared_norm() > T{0});

  std::vector<point3<T>> positions = {origin, origin + edge_u, origin + edge_v,
                                      origin + edge_u + edge_v};

  std::vector<TriangleIndices> triangles = {
      TriangleIndices{0, 1, 2},
      TriangleIndices{1, 3, 2},
  };

  return Mesh<T>{std::move(positions),
                 {},
                 std::move(triangles),
                 std::move(surface_material)};
}
