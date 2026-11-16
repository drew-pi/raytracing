#pragma once

#include <bound_box.h>
#include <interactions.h>
#include <mesh.h>
#include <triangle.h>

#ifdef RAYTRACER_TRAVERSAL_STATS
#include <diagnostic/stats.h>
#endif

#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#ifdef RAYTRACER_BVH_STATS
template <std::floating_point T> class BVH;
namespace diagnostic {
template <std::floating_point T> struct BVHStatistics;
template <std::floating_point T>
BVHStatistics<T> bvh_statistics(const BVH<T> &bvh);
} // namespace diagnostic
#endif

template <std::floating_point T> struct BVHNode {
  BoundingBox<T> box;

  // Combines two pieces of data (never both so this is fine)
  // internal: right-child index. leaf: first-primitive index.
  size_t offset = 0;
  std::uint32_t primitive_count = 0;
  std::uint8_t split_axis = 0;
};

template <std::floating_point T> class BVH {

private:
  std::vector<BVHNode<T>> nodes;
  std::vector<std::uint32_t> indexes;
  static constexpr T intersection_cost = T{0.5};
  static constexpr T traversal_cost = T{1};

  struct BuildPrimitive {
    BoundingBox<T> box;
    point3<T> centroid;
    std::uint32_t primitive_id;
  };

  struct SplitCandidate {
    int axis = -1;
    size_t position = 0;
    T sah = std::numeric_limits<T>::infinity();
    BoundingBox<T> left_bounds = BoundingBox<T>::empty();
    BoundingBox<T> right_bounds = BoundingBox<T>::empty();
  };

  static constexpr int bin_count = 16;

  struct BinnedSplitCandidate {
    int axis = -1;
    int bin = -1;
    T minimum = T{0};
    T maximum = T{0};
    T sah = std::numeric_limits<T>::infinity();
    BoundingBox<T> left_bounds = BoundingBox<T>::empty();
    BoundingBox<T> right_bounds = BoundingBox<T>::empty();
  };

#ifdef RAYTRACER_BVH_STATS
  template <std::floating_point U>
  friend diagnostic::BVHStatistics<U>
  diagnostic::bvh_statistics(const BVH<U> &bvh);
#endif

  [[nodiscard]] static std::vector<BuildPrimitive>
  make_build_primitives(const std::vector<point3<T>> &positions,
                        const std::vector<SceneTriangle> &triangles) {
    std::vector<BuildPrimitive> primitives;
    primitives.reserve(triangles.size());

    for (std::uint32_t primitive_id = 0; primitive_id < triangles.size();
         ++primitive_id) {
      const BoundingBox<T> box =
          triangle_bounding_box<T>(triangles[primitive_id].vertices, positions);

      primitives.push_back(BuildPrimitive{
          box,
          box.centroid(),
          primitive_id,
      });
    }

    return primitives;
  }

  [[nodiscard]] static BoundingBox<T>
  range_bounds(const std::vector<BuildPrimitive> &primitives, size_t start,
               size_t end) {
    assert(start < end);
    assert(end <= primitives.size());

    BoundingBox<T> bounds = primitives[start].box;

    for (size_t i = start + 1; i < end; ++i) {
      bounds = BoundingBox<T>(bounds, primitives[i].box);
    }

    return bounds;
  }

  void emit_leaf(const std::vector<BuildPrimitive> &primitives, size_t start,
                 size_t end, const BoundingBox<T> &bounds) {
    const size_t primitive_count = end - start;
    const size_t first_primitive = indexes.size();

    assert(primitive_count > 0);
    assert(primitive_count < std::numeric_limits<std::uint32_t>::max());

    for (size_t i = start; i < end; ++i) {
      indexes.push_back(primitives[i].primitive_id);
    }

    nodes.push_back(BVHNode<T>{
        bounds,
        first_primitive,
        static_cast<std::uint32_t>(primitive_count),
        0,
    });
  }

  [[nodiscard]] static int bin_index(const BuildPrimitive &primitive, T minimum,
                                     T maximum, int axis) {
    assert(maximum > minimum);
    assert(axis >= 0 && axis < 3);
    assert(bin_count > 0);

    const T normalized =
        (primitive.centroid[axis] - minimum) / (maximum - minimum);

    return std::clamp(static_cast<int>(normalized * T(bin_count)), 0,
                      bin_count - 1);
  }

  BinnedSplitCandidate
  evaluate_binned_splits(std::vector<BuildPrimitive> &primitives, size_t start,
                         size_t end, int axis, T parent_surface_area) {

    struct Bin {
      BoundingBox<T> bounds = BoundingBox<T>::empty();
      std::uint32_t count = 0;
    };

    assert(start < end);
    assert(end <= primitives.size());
    assert(axis >= 0 && axis < 3);
    assert(parent_surface_area > T{0});

    T minimum = primitives[start].centroid[axis];
    T maximum = minimum;

    for (size_t i = start + 1; i < end; ++i) {
      minimum = std::min(minimum, primitives[i].centroid[axis]);
      maximum = std::max(maximum, primitives[i].centroid[axis]);
    }

    BinnedSplitCandidate best{
        .axis = axis,
        .minimum = minimum,
        .maximum = maximum,
    };

    // This axis cannot separate the primitives.
    if (minimum == maximum)
      return best;

    std::array<Bin, bin_count> bins{};

    for (size_t i = start; i < end; ++i) {
      Bin &bin = bins[bin_index(primitives[i], minimum, maximum, axis)];
      ++bin.count;
      bin.bounds = BoundingBox<T>(bin.bounds, primitives[i].box);
    }

    // suffix sweep (suffix[i] includes all i, i+1, ..., number_bins-1)
    std::array<BoundingBox<T>, bin_count> suffix_bounds;
    std::array<std::uint32_t, bin_count> suffix_counts{};

    suffix_bounds[bin_count - 1] = bins[bin_count - 1].bounds;
    suffix_counts[bin_count - 1] = bins[bin_count - 1].count;

    for (int i = bin_count - 2; i >= 0; --i) {
      suffix_bounds[i] = BoundingBox<T>(bins[i].bounds, suffix_bounds[i + 1]);
      suffix_counts[i] = bins[i].count + suffix_counts[i + 1];
    }

    // prefix sweep (prefix includes all 0, ..., i)
    BoundingBox<T> left_bounds = BoundingBox<T>::empty();
    std::uint32_t left_count = 0;

    for (int i = 0; i < bin_count - 1; ++i) {
      left_bounds = BoundingBox<T>(left_bounds, bins[i].bounds);
      left_count += bins[i].count;

      const std::uint32_t right_count = suffix_counts[i + 1];

      if (left_count == 0 || right_count == 0)
        continue;

      const T sah =
          (left_bounds.surface_area() / parent_surface_area) * T(left_count) +
          (suffix_bounds[i + 1].surface_area() / parent_surface_area) *
              T(right_count);

      if (sah < best.sah) {
        best.bin = i;
        best.sah = sah;
        best.left_bounds = left_bounds;
        best.right_bounds = suffix_bounds[i + 1];
      }
    }

    return best;
  }

  [[nodiscard]] static auto centroid_comparator(int axis) {
    assert(axis >= 0 && axis < 3);

    return [axis](const BuildPrimitive &a, const BuildPrimitive &b) {
      const T a_centroid = a.centroid[axis];
      const T b_centroid = b.centroid[axis];

      if (a_centroid < b_centroid)
        return true;
      if (b_centroid < a_centroid)
        return false;

      return a.primitive_id < b.primitive_id;
    };
  }

  [[nodiscard]] static SplitCandidate
  evaluate_axis_splits(std::vector<BuildPrimitive> &primitives, size_t start,
                       size_t end, int axis, T parent_surface_area,
                       std::vector<BoundingBox<T>> &right_bounds) {

    const size_t primitive_count = end - start;

    assert(start < end);
    assert(end <= primitives.size());
    assert(axis >= 0 && axis < 3);
    assert(right_bounds.size() >= primitive_count);

    std::sort(primitives.begin() + start, primitives.begin() + end,
              centroid_comparator(axis));

    right_bounds[primitive_count - 1] = primitives[end - 1].box;

    for (size_t offset = primitive_count - 1; offset > 0; --offset) {
      right_bounds[offset - 1] = BoundingBox<T>(
          primitives[start + offset - 1].box, right_bounds[offset]);
    }

    SplitCandidate best{
        .axis = axis,
        .position = 0,
        .sah = std::numeric_limits<T>::infinity(),
    };

    BoundingBox<T> left_bounds = primitives[start].box;

    for (size_t offset = 1; offset < primitive_count; ++offset) {
      const T sah =
          (left_bounds.surface_area() / parent_surface_area) * T(offset) +
          (right_bounds[offset].surface_area() / parent_surface_area) *
              T(primitive_count - offset);

      if (sah < best.sah) {
        best.position = start + offset;
        best.sah = sah;
        best.left_bounds = left_bounds;
        best.right_bounds = right_bounds[offset];
      }

      left_bounds = BoundingBox<T>(left_bounds, primitives[start + offset].box);
    }

    return best;
  }

  [[nodiscard]] SplitCandidate
  find_best_exact_split(std::vector<BuildPrimitive> &primitives, size_t start,
                        size_t end, T parent_surface_area,
                        std::vector<BoundingBox<T>> &right_bounds) {
    const size_t primitive_count = end - start;

    SplitCandidate best;

    for (int axis = 0; axis < 3; ++axis) {
      const SplitCandidate candidate = evaluate_axis_splits(
          primitives, start, end, axis, parent_surface_area, right_bounds);

      if (candidate.sah < best.sah)
        best = candidate;
    }

    assert(best.axis >= 0 && best.axis < 3);
    assert(best.position > start && best.position < end);
    assert(std::isfinite(best.sah));

    if (best.axis != 2) {
      std::sort(primitives.begin() + start, primitives.begin() + end,
                centroid_comparator(best.axis));
    }

    return best;
  }

  [[nodiscard]] SplitCandidate
  find_best_split(std::vector<BuildPrimitive> &primitives, size_t start,
                  size_t end, T parent_surface_area,
                  std::vector<BoundingBox<T>> &right_bounds) {

    BinnedSplitCandidate best;

    for (int axis = 0; axis < 3; ++axis) {
      const BinnedSplitCandidate candidate = evaluate_binned_splits(
          primitives, start, end, axis, parent_surface_area);

      if (candidate.sah < best.sah)
        best = candidate;
    }

    // No axis had a usable centroid extent or non-empty split.
    if (best.axis < 0 || best.bin < 0) {
      return find_best_exact_split(primitives, start, end, parent_surface_area,
                                   right_bounds);
    }

    // Partition at the chosen bin boundary; middle begins the right child.
    const auto middle =
        std::partition(primitives.begin() + start, primitives.begin() + end,
                       [&](const BuildPrimitive &primitive) {
                         return bin_index(primitive, best.minimum, best.maximum,
                                          best.axis) <= best.bin;
                       });

    const size_t position = static_cast<size_t>(middle - primitives.begin());

    assert(position > start && position < end);
    assert(std::isfinite(best.sah));

    return SplitCandidate{
        .axis = best.axis,
        .position = position,
        .sah = best.sah,
        .left_bounds = best.left_bounds,
        .right_bounds = best.right_bounds,
    };
  }

  void build_range(std::vector<BuildPrimitive> &primitives, size_t start,
                   size_t end, const BoundingBox<T> &bounds,
                   std::vector<BoundingBox<T>> &right_bounds) {
    assert(start < end);
    assert(end <= primitives.size());

    const size_t primitive_count = end - start;

    if (primitive_count <= 2) {
      emit_leaf(primitives, start, end, bounds);
      return;
    }

    const T parent_surface_area = bounds.surface_area();

    assert(std::isfinite(parent_surface_area));
    assert(parent_surface_area > T{0});

    const SplitCandidate split = find_best_split(
        primitives, start, end, parent_surface_area, right_bounds);

    const T leaf_cost = T(primitive_count) * intersection_cost;
    const T split_cost = traversal_cost + intersection_cost * split.sah;

    if (leaf_cost <= split_cost) {
      emit_leaf(primitives, start, end, bounds);
      return;
    }

    const size_t node_index = nodes.size();

    nodes.push_back(BVHNode<T>{
        bounds,
        0,
        0,
        static_cast<std::uint8_t>(split.axis),
    });

    build_range(primitives, start, split.position, split.left_bounds,
                right_bounds);

    const size_t right_child = nodes.size();

    build_range(primitives, split.position, end, split.right_bounds,
                right_bounds);

    nodes[node_index].offset = right_child;
  }

public:
  BVH() = default;

  BVH(const std::vector<point3<T>> &positions,
      const std::vector<SceneTriangle> &triangles) {
    assert(!triangles.empty());
    assert(triangles.size() < std::numeric_limits<std::uint32_t>::max());

    auto primitives = make_build_primitives(positions, triangles);

    indexes.reserve(triangles.size());
    nodes.reserve(2 * triangles.size() - 1);

    const BoundingBox<T> root_bounds =
        range_bounds(primitives, 0, primitives.size());
    std::vector<BoundingBox<T>> right_bounds(primitives.size());

    build_range(primitives, 0, primitives.size(), root_bounds, right_bounds);
  }

  [[nodiscard]] constexpr BoundingBox<T> bounding_box() const {
    assert(!nodes.empty());
    return nodes.front().box;
  }

  [[nodiscard]] std::optional<BVHHit<T>>
  hit(const Ray<T> &r, Interval<T> interval,
      std::span<const point3<T>> positions,
      std::span<const SceneTriangle> triangles) const {
    assert(!nodes.empty());

#ifdef RAYTRACER_TRAVERSAL_STATS
    auto *stats = diagnostic::active_counters;
#endif

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
#ifdef RAYTRACER_TRAVERSAL_STATS
      if (stats)
        ++stats->box_tests;
#endif
      if (node.box.hit(r, interval, axis_direction_inv)) {
#ifdef RAYTRACER_TRAVERSAL_STATS
        if (stats)
          ++stats->box_hits;
#endif

        // internal node
        if (node.primitive_count == 0) {
          assert(node.offset < nodes.size());
          assert(index + 1 < nodes.size());
          assert(node.split_axis < 3);

          const size_t left = index + 1;
          const size_t right = node.offset;

          // Defer one child; visit the preferred child without a push/pop
          // pair.

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
#ifdef RAYTRACER_TRAVERSAL_STATS
        if (stats)
          ++stats->leaf_visits;
#endif
        for (size_t i = 0; i < node.primitive_count; ++i) {
          const size_t primitive_index = node.offset + i;
          const auto triangle_id = indexes[primitive_index];
          const auto &triangle = triangles[triangle_id];
          const auto &vertices = triangle.vertices;

#ifdef RAYTRACER_TRAVERSAL_STATS
          if (stats)
            ++stats->triangle_tests;
#endif
          auto candidate = intersect_triangle<T>(
              r, interval, positions[vertices.v0], positions[vertices.v1],
              positions[vertices.v2]);

          if (candidate) {
#ifdef RAYTRACER_TRAVERSAL_STATS
            if (stats)
              ++stats->triangle_hits;
#endif
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

#ifdef RAYTRACER_BVH_STATS
#include <diagnostic/bvh_stats.h>
#endif
