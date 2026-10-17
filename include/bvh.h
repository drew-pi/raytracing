#pragma once

#include <bound_box.h>
#include <mesh.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

template <std::floating_point T> struct BVHNode {
  BoundingBox<T> box;

  // Combines two pieces of data (never both so this is fine)
  // internal: right-child index. leaf: first-primitive index.
  size_t offset = 0;
  std::uint32_t primitive_count = 0;
  std::uint8_t split_axis = 0;
};

template <std::floating_point T> struct BVHHit {
  std::uint32_t triangle_id;
  T t;
  T u;
  T v;
};

template <std::floating_point T> class BVH {

private:
  std::vector<BVHNode<T>> nodes;
  std::vector<std::uint32_t> indexes;

  void build(const std::vector<point3<T>> &positions,
             const std::vector<SceneTriangle> &triangles,
             std::vector<std::uint32_t> &triangle_ids, size_t start,
             size_t end) {

    assert(start < end);
    assert(end <= triangle_ids.size());

    const size_t triangle_count = end - start;

    if (triangle_count <= 2) {
      // leaf node so offset identifies the first in entry in the indexes array
      const size_t first_primitive = indexes.size();

      const auto &first_triangle = triangles[triangle_ids[start]].vertices;

      auto leaf_bounds = Mesh<T>::bounding_box(first_triangle, positions);

      if (triangle_count == 2) {
        const auto &second_triangle =
            triangles[triangle_ids[start + 1]].vertices;

        leaf_bounds = BoundingBox<T>(
            leaf_bounds, Mesh<T>::bounding_box(second_triangle, positions));
      }

      nodes.push_back(BVHNode<T>{
          leaf_bounds,
          first_primitive,
          static_cast<std::uint32_t>(triangle_count),
          0,
      });

      for (size_t i = start; i < end; ++i) {
        indexes.push_back(triangle_ids[i]);
      }

      return;
    }

    // not a leaf case
    struct BuildEntry {
      BoundingBox<T> box;
      point3<T> centroid;
      size_t original_index;
    };

    // Cache boxes and centroids contiguously to avoid repeated primitive
    // pointer dereferences during sorting and SAH evaluation.
    std::vector<BuildEntry> entries;
    entries.reserve(triangle_count);

    const auto &first_triangle = triangles[triangle_ids[start]].vertices;
    const BoundingBox<T> box = Mesh<T>::bounding_box(first_triangle, positions);
    entries.push_back(BuildEntry{
        box,
        box.centroid(),
        start,
    });

    // get parent surface area
    BoundingBox<T> parent_bounding_box = entries.front().box;

    // loop combines two things
    // - calculates parent surface area
    // caches boxes and centroids contiguously
    for (size_t i = start + 1; i < end; ++i) {
      auto &current_triangle = triangles[triangle_ids[i]].vertices;
      const BoundingBox<T> current_box =
          Mesh<T>::bounding_box(current_triangle, positions);

      parent_bounding_box = BoundingBox<T>(parent_bounding_box, current_box);

      entries.push_back(BuildEntry{
          current_box,
          current_box.centroid(),
          i,
      });
    }

    const T parent_surface_area = parent_bounding_box.surface_area();

    assert(std::isfinite(parent_surface_area) && parent_surface_area > T{0});

    int best_axis = -1;
    size_t best_split_point = -1;
    T lowest_sah = std::numeric_limits<T>::infinity();

    auto comparator_for_axis = [](int axis) {
      return [axis](const BuildEntry &a, const BuildEntry &b) {
        const T ca = a.centroid[axis];
        const T cb = b.centroid[axis];

        if (ca < cb)
          return true;
        if (cb < ca)
          return false;

        return a.original_index < b.original_index;
      };
    };

    std::vector<BoundingBox<T>> right_boxes(triangle_count,
                                            BoundingBox<T>::empty());

    for (int axis = 0; axis < 3; ++axis) {
      std::sort(entries.begin(), entries.end(), comparator_for_axis(axis));

      // Build suffix bounds: right_boxes[k] encloses entries [k, span).
      right_boxes[triangle_count - 1] = entries[triangle_count - 1].box;

      for (size_t k = triangle_count - 1; k > 0; --k) {
        right_boxes[k - 1] = BoundingBox<T>(entries[k - 1].box, right_boxes[k]);
      }

      // Start the left-side bounds with the first entry; expand as the split
      // advances.
      BoundingBox<T> left_box = entries[0].box;

      for (size_t k = 1; k < triangle_count; ++k) {
        const BoundingBox<T> &right_box = right_boxes[k];

        const T sah = (left_box.surface_area() / parent_surface_area) * T(k) +
                      (right_box.surface_area() / parent_surface_area) *
                          T(triangle_count - k);

        if (sah < lowest_sah) {
          lowest_sah = sah;
          best_axis = axis;
          best_split_point = start + k;
        }

        left_box = BoundingBox<T>(left_box, entries[k].box);
      }
    }

    assert(best_axis >= 0 && best_axis < 3);
    assert(best_split_point > start && best_split_point < end);

    // leaf creation criterion
    const T C_isect = T{0.5}; // relative cost of a primitive intersection
    const T C_trav = T{1};    // relative cost of traversing an internal node

    const T leaf_cost = T(triangle_count) * C_isect;
    const T split_cost = C_trav + C_isect * lowest_sah;

    if (leaf_cost <= split_cost) {
      size_t primitive_index = indexes.size();

      for (size_t i = start; i < end; ++i) {
        indexes.push_back(triangle_ids[i]);
      }

      assert(triangle_count < std::numeric_limits<std::uint32_t>::max());
      nodes.push_back(BVHNode<T>{parent_bounding_box, primitive_index,
                                 static_cast<std::uint32_t>(triangle_count),
                                 0});

      return;
    }

    // if best axis is the last axis (z) then no need to run sorting again
    if (best_axis != 2)
      std::sort(entries.begin(), entries.end(), comparator_for_axis(best_axis));

    std::vector<std::uint32_t> reordered;
    reordered.reserve(triangle_count);

    for (const BuildEntry &entry : entries) {
      reordered.push_back(triangle_ids[entry.original_index]);
    }

    std::copy(reordered.begin(), reordered.end(), triangle_ids.begin() + start);

    // Append the parent before any of the subtrees.
    const size_t current_entry_index = nodes.size();
    nodes.push_back(BVHNode<T>{parent_bounding_box, 0, 0,
                               static_cast<std::uint8_t>(best_axis)});

    // The left child's root is now at current_entry_index + 1.
    build(positions, triangles, triangle_ids, start, best_split_point);

    // The right child's root will be at the next available position.
    const size_t right_index = nodes.size();
    build(positions, triangles, triangle_ids, best_split_point, end);

    // Use the index: recursive appends may have invalidated references.
    nodes[current_entry_index].offset = right_index;
  }

public:
  BVH() = default;

  BVH(const std::vector<point3<T>> &positions,
      const std::vector<SceneTriangle> &triangles) {

    assert(!triangles.empty());
    assert(triangles.size() < std::numeric_limits<std::uint32_t>::max());

    std::vector<std::uint32_t> triangle_ids(triangles.size());
    std::iota(triangle_ids.begin(), triangle_ids.end(), std::uint32_t{0});

    indexes.reserve(triangles.size());
    build(positions, triangles, triangle_ids, 0, triangle_ids.size());
  }

  [[nodiscard]] constexpr BoundingBox<T> bounding_box() const {
    return nodes.front().box;
  }

  [[nodiscard]] std::optional<BVHHit<T>>
  hit(const Ray<T> &r, Interval<T> interval,
      std::span<const point3<T>> positions,
      std::span<const SceneTriangle> triangles) const {

    // Each thread gets its own persistent stack, reused across calls.
    // clear() removes elements but preserves capacity to avoid reallocations.
    thread_local std::vector<size_t> stack;
    stack.clear();

    std::optional<BVHHit<T>> closest_hit;
    const auto axis_direction_inv = r.invert_direction();

    size_t index = 0;

    while (true) {
      assert(index < nodes.size());
      const BVHNode<T> &node = nodes[index];

      // Test the box
      if (node.box.hit(r, interval, axis_direction_inv)) {

        // internal node
        if (node.primitive_count == 0) {
          assert(node.offset < nodes.size());
          assert(index + 1 < nodes.size());
          assert(node.split_axis < 3);

          const size_t left = index + 1;
          const size_t right = node.offset;

          // Defer one child; visit the preferred child without a push/pop pair.

          // Heuristic for skipping less likely subtrees
          //
          // Try the lower-coordinate child first for positive directions, and
          // the higher-coordinate child first for negative directions. Push
          // order is reversed because the stack is LIFO.
          if (r.direction()[node.split_axis] < T{0}) {
            stack.push_back(left);
            index = right;
          } else {
            stack.push_back(right);
            index = left;
          }

          continue;
        }

        // leaf
        for (size_t i = 0; i < node.primitive_count; ++i) {
          const size_t primitive_index = node.offset + i;
          const auto triangle_id = indexes[primitive_index];
          const auto &triangle = triangles[triangle_id];
          const auto &vertices = triangle.vertices;

          auto candidate = Mesh<T>::intersect_triangle(
              r, interval, positions[vertices.v0], positions[vertices.v1],
              positions[vertices.v2]);

          if (candidate) {
            interval = Interval<T>{interval.min(), candidate->t};
            closest_hit = BVHHit<T>{
                triangle_id,
                candidate->t,
                candidate->u,
                candidate->v,
            };
          }
        }
      }

      // A miss or completed leaf finishes this branch.
      if (stack.empty())
        break;

      index = stack.back();
      stack.pop_back();
    }

    return closest_hit;
  }
};
