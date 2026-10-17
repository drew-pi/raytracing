#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mesh.h>

namespace sphere_detail {

using MidpointCache = std::unordered_map<std::uint64_t, std::uint32_t>;

template <std::floating_point T> struct SphereBuffers {
  std::vector<point3<T>> positions;
  std::vector<vec3<T>> normals;
  std::vector<TriangleIndices> triangles;
};

//
// For icosphere generation
//
template <std::floating_point T>
[[nodiscard]] static SphereBuffers<T> initialize_unit_icosahedron() {
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

  // The buffers are mutable runtime data, initialized from constant data.
  SphereBuffers<T> mesh;
  mesh.positions.assign(seed_positions.begin(), seed_positions.end());
  mesh.triangles.assign(seed_triangles.begin(), seed_triangles.end());

  // Eligible for named return value optimization (NRVO).
  // If NRVO is not applied, the mesh can be moved instead of copied.
  return mesh;
}

template <std::floating_point T>
[[nodiscard]] static int reserve_icosphere_vertices(SphereBuffers<T> &mesh,
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

template <std::floating_point T>
[[nodiscard]] static std::uint32_t
get_midpoint_index(SphereBuffers<T> &mesh, MidpointCache &cache,
                   std::uint32_t a, std::uint32_t b) {
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

template <std::floating_point T>
static void subdivide_sphere_once(SphereBuffers<T> &mesh) {
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

} // namespace sphere_detail

// Uses a icosphere approximation for a true sphere
template <std::floating_point T>
[[nodiscard]] static Mesh<T>
make_sphere(const point3<T> &center, T radius, int subdivisions,
            std::unique_ptr<Material<T>> surface_material) {
  assert(radius > T{0});
  assert(subdivisions >= 0);
  assert(surface_material != nullptr);

  auto sphere = sphere_detail::initialize_unit_icosahedron<T>();

  // Clamp to the supported subdivision count and reserve vertex storage.
  subdivisions =
      sphere_detail::reserve_icosphere_vertices(sphere, subdivisions);

  for (int level = 0; level < subdivisions; ++level) {
    sphere_detail::subdivide_sphere_once(sphere);
  }

  sphere.normals.reserve(sphere.positions.size());

  // Convert the unit sphere into the requested world-space sphere.
  for (auto &position : sphere.positions) {

    // find the normal at each vertex position
    const auto direction = unit_vector(position);
    sphere.normals.push_back(direction);

    position = center + radius * position;
  }

  return Mesh<T>(std::move(sphere.positions), std::move(sphere.normals),
                 std::move(sphere.triangles), std::move(surface_material));
}

// Uses a UV sphere approximation (declaration only; not implemented).
template <std::floating_point T>
[[nodiscard]] static Mesh<T>
make_sphere(const point3<T> &center, T radius, int slices, int stacks,
            std::unique_ptr<Material<T>> surface_material);
