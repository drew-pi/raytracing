#pragma once

#include <concepts>

#include <vec3.h>

template <std::floating_point T> class Material;

template <std::floating_point T> struct SurfaceInteraction {
  point3<T> p;
  T t;

  vec3<T> geometric_normal;
  vec3<T> shading_normal;

  std::reference_wrapper<const Material<T>> material;
  bool front_face;
};

template <std::floating_point T> struct BVHHit {
  std::uint32_t triangle_id;
  T t;
  T u;
  T v;
};

template <std::floating_point T> struct TriangleIntersectionRecord {
  T t;
  T u;
  T v;
};
