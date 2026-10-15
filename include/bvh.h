#pragma once

#include <bound_box.h>
#include <hittable.h>
#include <hittable_list.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

// template <std::floating_point T> struct BVHEntry { // 80 bytes per node
//   BoundingBox<T> box;

//   size_t right_child = 0; // Internal: left child is this node's index + 1.

//   size_t first_primitive = 0; // Leaf: start in the shared primitive vector.
//   size_t primitive_count = 0; // Zero means internal; positive means leaf.

//   int split_axis = 0; // the best axis
// };

template <std::floating_point T> struct BVHEntry {
  BoundingBox<T> box;

  // Combines two pieces of data (never both so this is fine)
  // internal: right-child index. leaf: first-primitive index.
  size_t offset = 0;
  std::uint32_t primitive_count = 0;
  std::uint8_t split_axis = 0;
};

template <std::floating_point T> class BVHNode : public Hittable<T> {

private:
  std::vector<BVHEntry<T>> nodes;
  std::vector<std::unique_ptr<Hittable<T>>> primitives;
  mutable std::vector<size_t> traversal_stack;

  void build(std::vector<std::unique_ptr<Hittable<T>>> &objects, size_t start,
             size_t end) {

    assert(start < end);
    assert(end <= objects.size());

    size_t span = end - start;

    // base case
    if (span == 1) {
      size_t primitive_index = primitives.size();
      nodes.push_back(
          BVHEntry{objects[start]->bounding_box(), primitive_index, 1, 0});
      primitives.push_back(std::move(objects[start]));
      return;
    }
    // second base case
    if (span == 2) {
      size_t primitive_left_index = primitives.size();
      nodes.push_back(
          BVHEntry<T>{BoundingBox<T>(objects[start]->bounding_box(),
                                     objects[start + 1]->bounding_box()),
                      primitive_left_index, 2, 0});
      primitives.push_back(std::move(objects[start]));
      primitives.push_back(std::move(objects[start + 1]));
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
    entries.reserve(span);

    const BoundingBox<T> box = objects[start]->bounding_box();
    entries.push_back(BuildEntry{
        box,
        box.centroid(),
        start,
    });

    // get parent surface area
    BoundingBox<T> parent_bounding_box = entries.front().box;

    for (size_t i = start + 1; i < end; ++i) {
      const BoundingBox<T> box = objects[i]->bounding_box();
      parent_bounding_box = BoundingBox<T>(parent_bounding_box, box);

      entries.push_back(BuildEntry{
          box,
          box.centroid(),
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

    std::vector<BoundingBox<T>> right_boxes(span, BoundingBox<T>::empty());

    for (int axis = 0; axis < 3; ++axis) {
      std::sort(entries.begin(), entries.end(), comparator_for_axis(axis));

      // Build suffix bounds: right_boxes[k] encloses entries [k, span).
      right_boxes[span - 1] = entries[span - 1].box;

      for (size_t k = span - 1; k > 0; --k) {
        right_boxes[k - 1] = BoundingBox<T>(entries[k - 1].box, right_boxes[k]);
      }

      // Start the left-side bounds with the first entry; expand as the split
      // advances.
      BoundingBox<T> left_box = entries[0].box;

      for (size_t k = 1; k < span; ++k) {
        const BoundingBox<T> &right_box = right_boxes[k];

        const T sah =
            (left_box.surface_area() / parent_surface_area) * T(k) +
            (right_box.surface_area() / parent_surface_area) * T(span - k);

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

    const T leaf_cost = T(span) * C_isect;
    const T split_cost = C_trav + C_isect * lowest_sah;

    if (leaf_cost <= split_cost) {
      size_t primitive_index = primitives.size();

      for (size_t i = start; i < end; ++i) {
        primitives.push_back(std::move(objects[i]));
      }

      assert(span < std::numeric_limits<std::uint32_t>::max());
      nodes.push_back(BVHEntry<T>{parent_bounding_box, primitive_index,
                                  static_cast<std::uint32_t>(span), 0});

      return;
    }

    std::sort(entries.begin(), entries.end(), comparator_for_axis(best_axis));

    std::vector<std::unique_ptr<Hittable<T>>> reordered;
    reordered.reserve(span);

    for (const BuildEntry &entry : entries) {
      reordered.push_back(std::move(objects[entry.original_index]));
    }

    for (size_t k = 0; k < span; ++k) {
      objects[start + k] = std::move(reordered[k]);
    }

    // Append the parent before any of the subtrees.
    const size_t current_entry_index = nodes.size();
    nodes.push_back(BVHEntry<T>{parent_bounding_box, 0, 0,
                                static_cast<std::uint8_t>(best_axis)});

    // The left child's root is now at current_entry_index + 1.
    build(objects, start, best_split_point);

    // The right child's root will be at the next available position.
    const size_t right_index = nodes.size();
    build(objects, best_split_point, end);

    // Use the index: recursive appends may have invalidated references.
    nodes[current_entry_index].offset = right_index;
  }

public:
  BVHNode(HittableList<T> objects)
      : BVHNode(objects.objects, 0, objects.objects.size()) {}

  BVHNode(std::vector<std::unique_ptr<Hittable<T>>> &objects, size_t start,
          size_t end) {

    assert(start < end);
    assert(end <= objects.size());
    primitives.reserve(end - start);
    build(objects, start, end);
  }

  [[nodiscard]] constexpr BoundingBox<T> bounding_box() const override {
    return nodes.front().box;
  }

  [[nodiscard]] std::optional<HitRecord<T>>
  hit(const Ray<T> &r, Interval<T> interval) const override {

    auto &stack = traversal_stack;
    stack.clear();

    std::optional<HitRecord<T>> closest_hit;
    const auto axis_direction_inv = r.invert_direction();

    size_t index = 0;

    while (true) {
      assert(index < nodes.size());
      const BVHEntry<T> &node = nodes[index];

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

          if (auto candidate = primitives[primitive_index]->hit(r, interval)) {
            interval = Interval<T>{interval.min(), candidate->t};
            closest_hit = candidate;
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
