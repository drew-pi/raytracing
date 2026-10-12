#pragma once

#include <bound_box.h>
#include <hittable.h>
#include <interval.h>

#include <concepts>
#include <memory>
#include <utility>
#include <vector>

template <std::floating_point T> class HittableList : public Hittable<T> {

private:
  BoundingBox<T> _bounding_box;

public:
  std::vector<std::unique_ptr<Hittable<T>>> objects;

  HittableList() {};
  HittableList(std::unique_ptr<Hittable<T>> object) { add(std::move(object)); }

  void clear() {
    objects.clear();
    _bounding_box = BoundingBox<T>();
  }

  void add(std::unique_ptr<Hittable<T>> object) {
    _bounding_box = BoundingBox(_bounding_box, object->bounding_box());
    objects.push_back(std::move(object));
  }

  [[nodiscard]] constexpr BoundingBox<T> bounding_box() const override {
    return _bounding_box;
  }

  [[nodiscard]] std::optional<HitRecord<T>>
  hit(const Ray<T> &r, Interval<T> interval) const override {

    std::optional<HitRecord<T>> closest_hit;

    for (const auto &object : objects) {
      if (auto candidate = object->hit(r, interval)) {
        // constantly restricting the interval by shrinking the upper bound
        interval = Interval<T>{interval.min(), candidate->t};
        closest_hit = candidate;
      }
    }

    return closest_hit;
  }
};
