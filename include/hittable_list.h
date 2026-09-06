#pragma once

#include <hittable.h>
#include <interval.h>

#include <concepts>
#include <memory>
#include <utility>
#include <vector>

template <std::floating_point T> class HittableList : public Hittable<T> {

public:
  std::vector<std::unique_ptr<Hittable<T>>> objects;

  HittableList() {};
  HittableList(std::unique_ptr<Hittable<T>> object) { add(std::move(object)); }

  void clear() { objects.clear(); }

  void add(std::unique_ptr<Hittable<T>> object) {
    objects.push_back(std::move(object));
  }

  [[nodiscard]] std::optional<HitRecord<T>>
  hit(const Ray<T> &r, Interval<T> interval) const override {

    std::optional<HitRecord<T>> closest_hit;

    for (const auto &object : objects) {
      if (auto candidate = object->hit(r, interval)) {
        interval = Interval<T>{interval.min(), candidate->t};
        closest_hit = candidate;
      }
    }

    return closest_hit;
  }
};
