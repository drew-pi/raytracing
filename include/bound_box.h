#pragma once

#include <concepts>
#include <ray.h>

#include <interval.h>

template <std::floating_point T> class BoundingBox {

private:
  Interval<T> x, y, z;

public:
  constexpr BoundingBox<T>() noexcept {}

  constexpr BoundingBox<T>(const Interval<T> &x, const Interval<T> &y,
                           const Interval<T> &z) noexcept
      : x(x), y(y), z(z) {}

  constexpr BoundingBox<T>(const point3<T> p1, const point3<T> p2) noexcept {
    // p1 and p2 are extrema for the bounding box

    x = (p1.x() <= p2.x()) ? Interval(p1.x(), p2.x())
                           : Interval(p2.x(), p1.x());
    y = (p1.y() <= p2.y()) ? Interval(p1.y(), p2.y())
                           : Interval(p2.y(), p1.y());
    z = (p1.z() <= p2.z()) ? Interval(p1.z(), p2.z())
                           : Interval(p2.z(), p1.z());
  }

  constexpr BoundingBox<T>(const BoundingBox<T> box1,
                           const BoundingBox<T> box2) noexcept {
    x = Interval(box1.x_axis(), box2.x_axis());
    y = Interval(box1.y_axis(), box2.y_axis());
    z = Interval(box1.z_axis(), box2.z_axis());
  }

  constexpr const Interval<T> &get_axis(int n) const noexcept {
    if (n == 1)
      return y;
    if (n == 2)
      return z;
    return x;
  }

  [[nodiscard]] constexpr const Interval<T> &x_axis() const noexcept {
    return x;
  }

  [[nodiscard]] constexpr const Interval<T> &y_axis() const noexcept {
    return y;
  }

  [[nodiscard]] constexpr const Interval<T> &z_axis() const noexcept {
    return z;
  }

  [[nodiscard]] constexpr int longest_axis() const {

    if (x.size() > y.size()) {
      return x.size() > z.size() ? 0 : 2;
    }

    return y.size() > z.size() ? 1 : 2;
  }

  constexpr bool hit(const Ray<T> &ray, Interval<T> interval) const noexcept {

    for (int axis = 0; axis < 3; axis++) {
      const Interval<T> &a = get_axis(axis);

      const T origin = ray.origin()[axis];
      const T direction = ray.direction()[axis];

      // need to check direction not 0 because later dividing by 0
      if (direction == T{0}) {
        // if outside of the interval, will never reach the box since direction
        // is 0
        if (!a.contains(origin)) {
          return false;
        }

        // if inside the bounding box for this axis, need to check all others
        continue;
      }

      const T axis_direction_inv = T{1} / direction;

      T t0 = (a.min() - origin) * axis_direction_inv;
      T t1 = (a.max() - origin) * axis_direction_inv;

      // lower bound can only increase, upper bound can only decrease
      interval.minimum = std::max(interval.min(), std::min(t0, t1));
      interval.maximum = std::min(interval.max(), std::max(t0, t1));

      if (interval.maximum <= interval.minimum)
        return false;
    }

    return true;
  }

  [[nodiscard]] static constexpr BoundingBox<T> empty() noexcept {
    return BoundingBox<T>(Interval<T>::empty(), Interval<T>::empty(),
                          Interval<T>::empty());
  }

  [[nodiscard]] static constexpr BoundingBox<T> universe() noexcept {
    return BoundingBox<T>(Interval<T>::universe(), Interval<T>::universe(),
                          Interval<T>::universe());
  }
};
