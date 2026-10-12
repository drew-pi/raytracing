#pragma once

#include <bound_box.h>
#include <hittable.h>
#include <hittable_list.h>

#include <algorithm>
#include <concepts>
#include <memory>

template <std::floating_point T> class BVHNode : public Hittable<T> {

private:
  std::unique_ptr<Hittable<T>> left;
  std::unique_ptr<Hittable<T>> right;
  BoundingBox<T> _bounding_box;

public:
  BVHNode(HittableList<T> objects)
      : BVHNode(objects.objects, 0, objects.objects.size()) {}

  BVHNode(std::vector<std::unique_ptr<Hittable<T>>> &objects, size_t start,
          size_t end) {

    assert(start < end);
    assert(end <= objects.size());

    size_t span = end - start;

    // base case
    if (span == 1) {
      left = std::move(objects[start]);
      _bounding_box = left->bounding_box();
      return;
    }
    // second base case
    if (span == 2) {
      left = std::move(objects[start]);
      right = std::move(objects[start + 1]);
      _bounding_box =
          BoundingBox<T>(left->bounding_box(), right->bounding_box());
      return;
    }

    // not a leaf case
    struct BuildEntry {
      BoundingBox<T> box;
      point3<T> centroid;
      size_t original_index;
    };

    std::vector<BuildEntry> entries;
    entries.reserve(span);

    for (size_t i = start; i < end; ++i) {
      const BoundingBox<T> box = objects[i]->bounding_box();

      entries.push_back(BuildEntry{
          box,
          box.centroid(),
          i,
      });
    }

    _bounding_box = entries.front().box;

    for (size_t k = 1; k < span; ++k) {
      _bounding_box = BoundingBox<T>(_bounding_box, entries[k].box);
    }

    const T parent_surface_area = _bounding_box.surface_area();

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

      right_boxes[span - 1] = entries[span - 1].box;

      for (size_t k = span - 1; k > 0; --k) {
        right_boxes[k - 1] = BoundingBox<T>(entries[k - 1].box, right_boxes[k]);
      }

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
      auto leaf = std::make_unique<HittableList<T>>();
      leaf->objects.reserve(span);

      for (size_t i = start; i < end; ++i) {
        leaf->add(std::move(objects[i]));
      }

      left = std::move(leaf);
      // right stays null
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

    left = std::make_unique<BVHNode<T>>(objects, start, best_split_point);

    right = std::make_unique<BVHNode<T>>(objects, best_split_point, end);

    _bounding_box = BoundingBox<T>(left->bounding_box(), right->bounding_box());
  }

  //
  // END CONSTRUCTORS
  //

  [[nodiscard]] constexpr BoundingBox<T> bounding_box() const override {
    return _bounding_box;
  }

  [[nodiscard]] std::optional<HitRecord<T>>
  hit(const Ray<T> &r, Interval<T> interval) const override {

    if (!_bounding_box.hit(r, interval))
      return std::nullopt;

    std::optional<HitRecord<T>> left_hit = left->hit(r, interval);

    // right can be empty in a one object leaf
    if (!right)
      return left_hit;

    const T right_max = left_hit ? left_hit->t : interval.max();
    std::optional<HitRecord<T>> right_hit =
        right->hit(r, Interval<T>(interval.min(), right_max));

    return right_hit ? right_hit : left_hit;
  }
};
