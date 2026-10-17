#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <material.h>

struct TriangleIndices {
  std::uint32_t v0;
  std::uint32_t v1;
  std::uint32_t v2;
};

struct SceneTriangle {
  TriangleIndices vertices;
  std::uint32_t material_id;
};

template <std::floating_point T> class Mesh {

private:
  std::vector<point3<T>> positions; // shared triangle vertexes
  // empty normals means flat shading, otherwise normals.size = positions.size
  std::vector<vec3<T>> normals; // normals at those vertexes
  std::vector<TriangleIndices> triangles;
  std::unique_ptr<Material<T>> material;

public:
  Mesh() = default;

  Mesh(std::vector<point3<T>> positions, std::vector<vec3<T>> normals,
       std::vector<TriangleIndices> triangles,
       std::unique_ptr<Material<T>> material)
      : positions(std::move(positions)), normals(std::move(normals)),
        triangles(std::move(triangles)), material(std::move(material)) {
    assert(this->normals.empty() ||
           this->normals.size() == this->positions.size());
    assert(this->material != nullptr);

    for (const auto &triangle : this->triangles) {
      assert(triangle.v0 < this->positions.size());
      assert(triangle.v1 < this->positions.size());
      assert(triangle.v2 < this->positions.size());
    }
  }

  //
  // helper methods for adding mesh to a scene
  //
  [[nodiscard]] std::unique_ptr<Material<T>> move_material() && noexcept {
    return std::move(material);
  }

  [[nodiscard]] size_t vertex_count() const noexcept {
    return positions.size();
  }

  [[nodiscard]] size_t triangle_count() const noexcept {
    return triangles.size();
  }

  void append_positions(std::vector<point3<T>> &root_positions) const {
    root_positions.insert(root_positions.end(), positions.begin(),
                          positions.end());
  }

  void append_normals(std::vector<vec3<T>> &destination) const {
    // make sure that the precondition is met
    assert(normals.empty() || normals.size() == positions.size());

    if (normals.empty()) {
      // use placeholders for non complex geometry
      // - cheap because only have a few triangles (quads, boxes)
      destination.insert(destination.end(), positions.size(),
                         vec3<T>{T{0}, T{0}, T{0}});
    } else {
      destination.insert(destination.end(), normals.begin(), normals.end());
    }
  }

  void append_triangles(std::vector<SceneTriangle> &destination,
                        std::uint32_t vertex_offset,
                        std::uint32_t material_index) const {

    for (const auto &triangle : triangles) {
      destination.push_back({
          {
              triangle.v0 + vertex_offset,
              triangle.v1 + vertex_offset,
              triangle.v2 + vertex_offset,
          },
          material_index,
      });
    }
  }
};
