#pragma once

#include <concepts>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <bvh.h>
#include <hittable_list.h>
#include <mesh.h>

template <std::floating_point T> class Scene {
public:
  Scene() = default;

  void build(std::vector<Mesh<T>> meshes) {
    for (auto &mesh : meshes) {
      add_mesh(std::move(mesh));
    }

    build_bvh();
  }

  [[nodiscard]] std::optional<HitRecord<T>> hit(const Ray<T> &ray,
                                                Interval<T> interval) const {
    return _bvh.hit(ray, interval, positions, triangles, materials);
  }

private:
  std::vector<point3<T>> positions;
  std::vector<vec3<T>> normals;
  std::vector<SceneTriangle> triangles;
  std::vector<std::unique_ptr<Material<T>>> materials;
  BVH<T> _bvh;

  // consumes the mesh object
  void add_mesh(Mesh<T> mesh) {

    // variable takes ownership of material
    auto material = std::move(mesh).move_material();

    // make sure everything is stable
    assert(material != nullptr);
    const auto max_u32_index = std::numeric_limits<std::uint32_t>::max();
    assert(positions.size() < max_u32_index &&
           mesh.vertex_count() < max_u32_index - positions.size());
    assert(materials.size() < max_u32_index);
    assert(mesh.triangle_count() < triangles.max_size() - triangles.size());

    const auto vertex_offset = static_cast<std::uint32_t>(positions.size());

    const auto material_id = static_cast<std::uint32_t>(materials.size());

    // Allocate before changing the logical contents.
    positions.reserve(positions.size() + mesh.vertex_count());
    triangles.reserve(triangles.size() + mesh.triangle_count());
    materials.reserve(materials.size() + 1);

    mesh.append_positions(positions);
    mesh.append_triangles(triangles, vertex_offset, material_id);
    materials.push_back(std::move(material));
  }

  void build_bvh() { _bvh = BVH<T>(positions, triangles); }
};
