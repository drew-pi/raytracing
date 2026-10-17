#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <hittable.h>
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

template <std::floating_point T> struct TriangleIntersectionRecord {
  T t;
  T u;
  T v;
};

using MidpointCache = std::unordered_map<std::uint64_t, std::uint32_t>;

template <std::floating_point T> class Mesh {

private:
  std::vector<point3<T>> positions; // shared triangle vertexes
  // empty normals means flat shading, otherwise normals.size = positions.size
  std::vector<vec3<T>> normals; // normals at those vertexes
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

  //
  // For icosphere generation
  //
  [[nodiscard]] static Mesh initialize_unit_icosahedron() {
    // Already-normalized coordinate magnitudes for a regular icosahedron:
    //
    //   phi = (1 + sqrt(5)) / 2
    //   a = 1   / sqrt(1 + phi^2)
    //   b = phi / sqrt(1 + phi^2)
    //
    // Thus a^2 + b^2 = 1, up to floating-point rounding.
    constexpr T a = static_cast<T>(0.525731112119133606025669084848L);
    constexpr T b = static_cast<T>(0.850650808352039932181540497063L);

    // The fixed seed geometry is compile-time data.
    // Its coordinates are permutations of (0, +/-a, +/-b).
    static constexpr std::array<point3<T>, 12> seed_positions{{
        {-a, b, T{0}},  // 0
        {a, b, T{0}},   // 1
        {-a, -b, T{0}}, // 2
        {a, -b, T{0}},  // 3
        {T{0}, -a, b},  // 4
        {T{0}, a, b},   // 5
        {T{0}, -a, -b}, // 6
        {T{0}, a, -b},  // 7
        {b, T{0}, -a},  // 8
        {b, T{0}, a},   // 9
        {-b, T{0}, -a}, // 10
        {-b, T{0}, a},  // 11
    }};

    // Each triple indexes seed_positions.
    // Counterclockwise winding viewed from outside makes the normals
    // computed by cross(v1 - v0, v2 - v0) point outward.
    static constexpr std::array<TriangleIndices, 20> seed_triangles{{
        {0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
        {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
        {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1},
    }};

    // The Mesh is a mutable runtime object, initialized from constant data.
    Mesh mesh;
    mesh.positions.assign(seed_positions.begin(), seed_positions.end());
    mesh.triangles.assign(seed_triangles.begin(), seed_triangles.end());

    // Eligible for named return value optimization (NRVO).
    // If NRVO is not applied, the mesh can be moved instead of copied.
    return mesh;
  }

  [[nodiscard]] static int reserve_icosphere_vertices(Mesh &mesh,
                                                      int subdivisions) {
    // In general:
    //   faces    = 20 * 4^n
    //   vertices = 10 * 4^n + 2

    const std::size_t max_vertices = std::min<std::size_t>(
        mesh.positions.max_size(), std::numeric_limits<std::uint32_t>::max());

    const std::size_t max_faces = mesh.triangles.max_size();

    // Even an unsubdivided icosahedron needs 12 vertices and 20 faces.
    assert(max_vertices >= 12 && max_faces >= 20);

    std::size_t factor = 1;
    int clamped_subdivisions = 0;

    // Grow toward the requested level, stopping before the next level
    // would exceed either the vertex-index range or a container limit.
    while (clamped_subdivisions < subdivisions) {
      // Check before multiplying so the calculations cannot overflow.
      if (factor > (max_vertices - 2) / 40 || factor > max_faces / 80) {
        break;
      }

      factor *= 4;
      ++clamped_subdivisions;
    }

    // Reserve enough room for the accepted subdivision count.
    mesh.positions.reserve(10 * factor + 2);

    return clamped_subdivisions;
  }

  [[nodiscard]] static std::uint32_t get_midpoint_index(Mesh &mesh,
                                                        MidpointCache &cache,
                                                        std::uint32_t a,
                                                        std::uint32_t b) {
    // Adjacent faces describe their shared edge in opposite directions.
    // Sorting ensures that (a,b) and (b,a) use the same cache entry.
    if (a > b) {
      std::swap(a, b);
    }

    // Pack the indices into a single edge identifier:
    // | a: upper 32 bits | b : lower 32 bits |
    const std::uint64_t key =
        (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint64_t>(b);

    // If the edge already exists, reuse its midpoint. Otherwise insert a
    // placeholder and fill in the new vertex index below.
    const auto [entry, inserted] = cache.try_emplace(key, 0);

    if (inserted) {
      const auto index = static_cast<std::uint32_t>(mesh.positions.size());

      // The ordinary midpoint lies inside the sphere:
      //
      //       a *-------m-------* b
      //                  |
      //                  | project radially outward
      //                  v
      //             sphere surface
      //
      // Algebraically, projection onto the unit sphere is:
      //
      //   midpoint = (a + b) / 2
      //   projected = midpoint / length(midpoint)
      //
      // The factor 1/2 cancels during normalization, so normalize(a + b)
      // gives the same result with less arithmetic.
      mesh.positions.push_back(
          unit_vector(mesh.positions[a] + mesh.positions[b]));

      entry->second = index;
    }

    return entry->second;
  }

  static void subdivide_sphere_once(Mesh &mesh) {
    std::vector<TriangleIndices> refined;
    refined.reserve(mesh.triangles.size() * 4);

    // Every edge needs exactly one new vertex, even though two faces use it.
    // For a closed triangular mesh, E = 3F/2:
    // three edges per face, with each edge counted twice.
    MidpointCache midpoints;
    midpoints.reserve((mesh.triangles.size() / 2) * 3);

    for (const auto &triangle : mesh.triangles) {
      const auto a = triangle.v0;
      const auto b = triangle.v1;
      const auto c = triangle.v2;

      const auto ab = get_midpoint_index(mesh, midpoints, a, b);
      const auto bc = get_midpoint_index(mesh, midpoints, b, c);
      const auto ca = get_midpoint_index(mesh, midpoints, c, a);

      // Replace one face with four:
      //
      //             a
      //            / \
      //           /   \
      //         ca-----ab
      //         / \   / \
      //        /   \ /   \
      //       c-----bc----b
      //
      // Three corner triangles:
      //   (a, ab, ca), (b, bc, ab), (c, ca, bc)
      //
      // One central triangle:
      //   (ab, bc, ca)
      //
      // This diagram shows connectivity. In 3D, the projected midpoints
      // bulge outward rather than remaining in the original face's plane.
      //
      // Preserve the parent's winding so all child normals face outward.
      refined.push_back({a, ab, ca});
      refined.push_back({b, bc, ab});
      refined.push_back({c, ca, bc});
      refined.push_back({ab, bc, ca});
    }

    // Take ownership of the new face buffer without copying its elements.
    // The old faces are released when 'refined' leaves this function.
    mesh.triangles.swap(refined);
  }

public:
  // Mesh() noexcept : {}
  Mesh() = default;

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

  //
  // general triangle logic
  //

  [[nodiscard]] static std::optional<TriangleIntersectionRecord<T>>
  intersect_triangle(const Ray<T> &ray, Interval<T> interval,
                     const point3<T> &v0, const point3<T> &v1,
                     const point3<T> &v2) {

    const vec3<T> direction = ray.direction();
    const auto origin = ray.origin();

    const auto e1 = v1 - v0;
    const auto e2 = v2 - v0;
    const auto normal = cross(e1, e2);
    const auto s = origin - v0;

    const auto w = cross(direction, s);

    const auto determinant = dot(-direction, normal);

    constexpr T epsilon = T{1e-8};
    if (std::abs(determinant) <= epsilon)
      return std::nullopt;

    const auto t_num = dot(s, normal);
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

    return TriangleIntersectionRecord<T>(t, u, v);
  }

  [[nodiscard]] static BoundingBox<T>
  bounding_box(const point3<T> &v0, const point3<T> &v1, const point3<T> &v2) {
    return BoundingBox<T>(v0, v1, v2);
  }

  [[nodiscard]] static BoundingBox<T>
  bounding_box(TriangleIndices triangle, std::span<const point3<T>> positions) {
    const auto &v0 = positions[triangle.v0];
    const auto &v1 = positions[triangle.v1];
    const auto &v2 = positions[triangle.v2];

    return BoundingBox<T>(v0, v1, v2);
  }

  [[nodiscard]] constexpr point3<T> centroid(const point3<T> &p1,
                                             const point3<T> &p2,
                                             const point3<T> &p3) noexcept {
    constexpr T one_third = T{1} / T{3}; // computed at compile time
    return point3<T>((p1.x() + p2.x() + p3.x()) * one_third,
                     (p1.y() + p2.y() + p3.y()) * one_third,
                     (p1.z() + p2.z() + p3.z()) * one_third);
  }

  [[nodiscard]] static point3<T>
  centroid(TriangleIndices triangle,
           std::span<const point3<T>> positions) noexcept {
    constexpr T one_third = T{1} / T{3}; // computed at compile time
    const auto &v0 = positions[triangle.v0];
    const auto &v1 = positions[triangle.v1];
    const auto &v2 = positions[triangle.v2];

    return (v0 + v1 + v2) * one_third;
  }

  //
  // static procedural generators
  //
  [[nodiscard]] static Mesh
  make_sphere(const point3<T> &center, T radius, int slices, int stacks,
              std::unique_ptr<Material<T>> surface_material);

  // Uses a icosphere approximation for a true sphere
  [[nodiscard]] static Mesh
  make_sphere(const point3<T> &center, T radius, int subdivisions,
              std::unique_ptr<Material<T>> surface_material) {
    assert(radius > T{0});
    assert(subdivisions >= 0);
    assert(surface_material != nullptr);

    Mesh sphere = initialize_unit_icosahedron();

    // Clamp to the supported subdivision count and reserve vertex storage.
    subdivisions = reserve_icosphere_vertices(sphere, subdivisions);

    for (int level = 0; level < subdivisions; ++level) {
      subdivide_sphere_once(sphere);
    }

    sphere.normals.reserve(sphere.positions.size());

    // Convert the unit sphere into the requested world-space sphere.
    for (auto &position : sphere.positions) {

      // find the normal at each vertex position
      const auto direction = unit_vector(position);
      sphere.normals.push_back(direction);

      position = center + radius * position;
    }

    sphere.material = std::move(surface_material);
    return sphere;
  }

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
