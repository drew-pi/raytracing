#pragma once

#include <cassert>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <hittable.h>
#include <material.h>

struct TriangleIndices {
  std::uint32_t v0;
  std::uint32_t v1;
  std::uint32_t v2;
};

template <std::floating_point T> class Mesh {

private:
  std::vector<point3<T>> positions;
  std::vector<TriangleIndices> triangles;
  std::unique_ptr<Material<T>> material;

  [[nodiscard]] static std::vector<TriangleIndices> box_triangle_indices() {
    return std::vector<TriangleIndices>{
        {0, 2, 1}, {2, 3, 1}, // -z
        {4, 5, 6}, {5, 7, 6}, // +z
        {0, 1, 4}, {1, 5, 4}, // -y
        {2, 6, 3}, {6, 7, 3}, // +y
        {0, 4, 2}, {4, 6, 2}, // -x
        {1, 3, 5}, {3, 7, 5}, // +x
    };
  }

public:
  // Mesh() noexcept : {}
  Mesh() = default;

  [[nodiscard]] static std::optional<HitRecord<T>>
  intersect_triangle(const Ray<T> &ray, Interval<T> interval,
                     const Material<T> &material, const point3<T> &v0,
                     const point3<T> &v1, const point3<T> &v2) {

    const vec3<T> direction = ray.direction();
    const auto origin = ray.origin();

    const auto e1 = v1 - v0;
    const auto e2 = v2 - v0;
    const auto normal = cross(e1, e2);
    const auto S = origin - v0;

    const auto w = cross(direction, S);

    const auto determinant = dot(-direction, normal);

    constexpr T epsilon = T{1e-8};
    if (std::abs(determinant) <= epsilon)
      return std::nullopt;

    const auto t_num = dot(S, normal);
    const auto u_num = dot(-e2, w);
    const auto v_num = dot(e1, w);

    const auto inverse_det = T{1} / determinant;

    const auto u = u_num * inverse_det;
    const auto v = v_num * inverse_det;
    const auto t = t_num * inverse_det;

    // make sure the intersection point lies inside the triangle (including
    // its edges) uses barycentric coords (both coords have to be larger than
    // 0 and sum to less than 1)
    if (!(u >= T{0} && v >= T{0} && u + v <= T{1})) {
      return std::nullopt;
    }

    // Reject hits outside the permitted ray range.
    if (!interval.surrounds(t))
      return std::nullopt;

    const auto outward_normal = unit_vector(normal);

    return HitRecord<T>(ray.at(t), t, material, ray, outward_normal);
  }

  [[nodiscard]] static Mesh
  make_sphere(const point3<T> &center, T radius, int slices, int stacks,
              std::unique_ptr<Material<T>> surface_material);

  [[nodiscard]] static Mesh
  make_aligned_box(const point3<T> &min_corner, const point3<T> &max_corner,
                   std::unique_ptr<Material<T>> surface_material) {

    assert(surface_material != nullptr);
    assert(min_corner[0] < max_corner[0] && min_corner[1] < max_corner[1] &&
           min_corner[2] < max_corner[2]);

    Mesh box;

    const T x0 = min_corner[0], y0 = min_corner[1], z0 = min_corner[2];
    const T x1 = max_corner[0], y1 = max_corner[1], z1 = max_corner[2];

    box.positions = {
        point3<T>(x0, y0, z0), // 0: min                  (min)
        point3<T>(x1, y0, z0), // 1: min + u              (max x)
        point3<T>(x0, y1, z0), // 2: min + v              (max y)
        point3<T>(x1, y1, z0), // 3: min + u + v          (max x, y)
        point3<T>(x0, y0, z1), // 4: min + w              (max z)
        point3<T>(x1, y0, z1), // 5: min + u + w          (max x, z)
        point3<T>(x0, y1, z1), // 6: min + v + w          (max y, z)
        point3<T>(x1, y1, z1), // 7: max                  (max)
    };

    box.triangles = box_triangle_indices();

    box.material = std::move(surface_material);
    return box;
  }

  [[nodiscard]] static Mesh
  make_box(const point3<T> origin, const vec3<T> edge_u, const vec3<T> edge_v,
           const vec3<T> edge_w,
           std::unique_ptr<Material<T>> surface_material) {
    // The edges must form a right-handed set, or every face comes out
    // inside-out.
    assert(dot(cross(edge_u, edge_v), edge_w) > 0);
    assert(surface_material != nullptr);

    Mesh box;
    box.positions = {
        origin,                            // 0
        origin + edge_u,                   // 1
        origin + edge_v,                   // 2
        origin + edge_u + edge_v,          // 3
        origin + edge_w,                   // 4
        origin + edge_u + edge_w,          // 5
        origin + edge_v + edge_w,          // 6
        origin + edge_u + edge_v + edge_w, // 7
    };
    box.triangles = box_triangle_indices();
    box.material = std::move(surface_material);
    return box;
  }

  [[nodiscard]] static Mesh
  make_quad(const point3<T> &origin, const vec3<T> &edge_u,
            const vec3<T> &edge_v,
            std::unique_ptr<Material<T>> surface_material) {

    assert(surface_material != nullptr);
    assert(cross(edge_u, edge_v).squared_norm() > T{0});

    Mesh quad;

    quad.positions = {origin, origin + edge_u, origin + edge_v,
                      origin + edge_u + edge_v};

    quad.triangles = {
        TriangleIndices{0, 1, 2},
        TriangleIndices{1, 3, 2},
    };

    quad.material = std::move(surface_material);

    return quad;
  }
};
