#pragma once

#include <cmath>
#include <concepts>
#include <ostream>

#include <interval.h>
#include <vec3.h>

template <std::floating_point T> using Color = vec3<T>;

template <std::floating_point T> T linear_to_gamma(T linear) noexcept {

  if (linear > 0) {
    return std::sqrt(linear);
  }
  return 0;
}

template <std::floating_point T>
void write_color(std::ostream &out, const Color<T> &pixel_color) {

  auto r = pixel_color.x();
  auto g = pixel_color.y();
  auto b = pixel_color.z();

  // Apply a linear to gamma transform for gamma 2
  r = linear_to_gamma(r);
  g = linear_to_gamma(g);
  b = linear_to_gamma(b);

  // Translate the [0,1] component values to the byte range [0,255].
  static const Interval<T> intensity(0, 0.999);
  int rbyte = int(256 * intensity.clamp(r));
  int gbyte = int(256 * intensity.clamp(g));
  int bbyte = int(256 * intensity.clamp(b));

  // Write out the pixel color components.
  out << rbyte << ' ' << gbyte << ' ' << bbyte << '\n';
}
