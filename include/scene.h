#pragma once

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <bvh.h>
#include <material.h>
#include <mesh.h>

template <std::floating_point T> class Scene {
public:
  Scene() = default;

  void build(std::vector<Mesh<T>> meshes) {

    // checking preconditions
    //
    constexpr std::size_t index_limit =
        std::numeric_limits<std::uint32_t>::max() - std::size_t{1};

    const auto vertex_limit = std::min({
        index_limit,
        positions.max_size(),
        normals.max_size(),
    });

    const auto triangle_limit = std::min(index_limit, triangles.max_size());

    const auto material_limit = std::min(index_limit, materials.max_size());

    std::size_t total_vertices = positions.size();
    std::size_t total_triangles = triangles.size();

    assert(total_vertices <= vertex_limit);
    assert(total_triangles <= triangle_limit);

    assert(materials.size() <= material_limit);
    assert(meshes.size() <= material_limit - materials.size());

    const std::size_t total_materials = materials.size() + meshes.size();

    for (const auto &mesh : meshes) {
      assert(mesh.vertex_count() <= vertex_limit - total_vertices);
      total_vertices += mesh.vertex_count();

      assert(mesh.triangle_count() <= triangle_limit - total_triangles);
      total_triangles += mesh.triangle_count();
    }
    //
    // end checking preconditions

    // Each array needs at most one capacity increase for this batch.
    positions.reserve(total_vertices);
    normals.reserve(total_vertices);
    triangles.reserve(total_triangles);
    materials.reserve(total_materials);

    for (auto &mesh : meshes) {
      add_mesh(std::move(mesh));
    }

    build_bvh();
  }

  [[nodiscard]] std::optional<SurfaceInteraction<T>>
  hit(const Ray<T> &ray, Interval<T> interval) const {

    const std::optional<BVHHit<T>> candidate =
        _bvh.hit(ray, interval, positions, triangles);

    if (!candidate)
      return std::nullopt;

    const uint32_t triangle_id = candidate->triangle_id;
    const T t = candidate->t;
    const T u = candidate->u;
    const T v = candidate->v;

    const auto &triangle = triangles[triangle_id];
    const auto &indices = triangle.vertices;

    const auto &p0 = positions[indices.v0];
    const auto &p1 = positions[indices.v1];
    const auto &p2 = positions[indices.v2];

    const auto geometric_outward = unit_vector(cross(p1 - p0, p2 - p0));

    const bool front_face = dot(ray.direction(), geometric_outward) < T{0};

    auto shading_outward = geometric_outward;

    // get the normals
    const auto &n0 = normals[indices.v0];
    const auto &n1 = normals[indices.v1];
    const auto &n2 = normals[indices.v2];

    // only get blended normal when vertex normals are not zero
    if (n0.squared_norm() > T{0} && n1.squared_norm() > T{0} &&
        n2.squared_norm() > T{0}) {

      // from the equation T(u,v) = (1-u-v) * v0 + u * v1 + v * v2
      const T w = T{1} - u - v;
      const auto blended = w * n0 + u * n1 + v * n2;

      // makes sure the blend is non zero
      if (blended.squared_norm() > T{0}) {
        shading_outward = unit_vector(blended);
      }

      // Keep shading and geometry in the same outward hemisphere.
      if (dot(shading_outward, geometric_outward) < T{0}) {
        shading_outward = -shading_outward;
      }
    }

    return SurfaceInteraction<T>{
        .p = ray.at(t),
        .t = t,
        .geometric_normal = front_face ? geometric_outward : -geometric_outward,
        .shading_normal = front_face ? shading_outward : -shading_outward,
        .material = std::cref(*materials[triangle.material_id]),
        .front_face = front_face,
    };
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

    // no preallocation because this is handled in the public method
    //
    // positions.reserve(positions.size() + mesh.vertex_count());
    // normals.reserve(normals.size() + mesh.vertex_count());
    // triangles.reserve(triangles.size() + mesh.triangle_count());
    // materials.reserve(materials.size() + 1);

    mesh.append_positions(positions);
    mesh.append_normals(normals);
    mesh.append_triangles(triangles, vertex_offset, material_id);
    materials.push_back(std::move(material));

    // make sure the precondition holds
    assert(normals.size() == positions.size());
  }

  void build_bvh() { _bvh = BVH<T>(positions, triangles); }
};
