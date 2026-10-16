#pragma once

#include <concepts>

#include <bvh.h>
#include <hittable_list.h>

template <std::floating_point T> class Scene {
public:
  explicit Scene(HittableList<T> objects) : _bvh(std::move(objects)) {}

  [[nodiscard]] std::optional<HitRecord<T>> hit(const Ray<T> &ray,
                                                Interval<T> interval) const {
    return _bvh.hit(ray, interval);
  }

private:
  BVH<T> _bvh;
};
