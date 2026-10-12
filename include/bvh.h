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
    // randomly choose an axis
    // sort the primitives (using std::sort)
    // put half in each subtree

    assert(start <= end);
    assert(end <= objects.size());

    size_t span = end - start;

    // base case
    if (span == 1) {
      left = std::move(objects[start]);
      _bounding_box = left->bounding_box();
      return;
    }

    // better heuristic for splitting
    BoundingBox<T> box = BoundingBox<T>::empty();
    for (size_t obj_idx = start; obj_idx < end; obj_idx++) {
      box = BoundingBox<T>(box, objects[obj_idx]->bounding_box());
    }

    // choose an axis
    int axis = box.longest_axis();

    auto comparator = [axis](const auto &a, const auto &b) {
      return a->bounding_box().get_axis(axis).min() <
             b->bounding_box().get_axis(axis).min();
    };

    std::sort(std::begin(objects) + start, std::begin(objects) + end,
              comparator);

    if (span == 2) {
      left = std::move(objects[start]);
      right = std::move(objects[start + 1]);
    } else {
      const size_t mid = start + span / 2;
      left = std::make_unique<BVHNode<T>>(objects, start, mid);
      right = std::make_unique<BVHNode<T>>(objects, mid, end);
    }

    _bounding_box = BoundingBox<T>(left->bounding_box(), right->bounding_box());
  }

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
