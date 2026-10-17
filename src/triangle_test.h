#pragma once

#include <cmath>
#include <memory>
#include <numbers>
#include <tuple>
#include <utility>
#include <vector>

#include <camera.h>
#include <color.h>
#include <material.h>
#include <mesh.h>
#include <mesh_generators/quad.h>
#include <mesh_generators/box.h>
#include <mesh_generators/sphere.h>
#include <vec3.h>

[[nodiscard]] inline std::tuple<Camera<double>, std::vector<Mesh<double>>>
create_triangle_test_scene() {
  using Point = point3<double>;
  using Vector = vec3<double>;
  using Colour = Color<double>;

  std::vector<Mesh<double>> meshes;
  meshes.reserve(11);
  const Colour white(0.73, 0.73, 0.73);
  const Colour red(0.65, 0.05, 0.05);
  const Colour green(0.12, 0.45, 0.15);
  auto diffuse = [](const Colour &color) {
    return std::make_unique<Lambertian<double>>(color);
  };
  auto quad = [&](const Point &origin, const Vector &u, const Vector &v,
                  const Colour &color) {
    meshes.push_back(make_quad<double>(origin, u, v, diffuse(color)));
  };

  // Interior: x in [-3, 3], y in [0, 6], z in [-6, 0].
  // Cross(u, v) points inward; front and ceiling remain open to the sky.
  quad(Point(-3, 0, 0), Vector(6, 0, 0), Vector(0, 0, -6), white); // +Y
  quad(Point(-3, 0, -6), Vector(6, 0, 0), Vector(0, 6, 0), white); // +Z
  quad(Point(-3, 0, 0), Vector(0, 0, -6), Vector(0, 6, 0), red);   // +X
  quad(Point(3, 0, -6), Vector(0, 0, 6), Vector(0, 6, 0), green);  // -X

  auto cuboid = [&](double center_x, double center_z, double width,
                    double height, double depth, double angle_degrees) {
    const double angle = angle_degrees * std::numbers::pi_v<double> / 180.0;
    const double cosine = std::cos(angle), sine = std::sin(angle);
    const Vector u(width * cosine, 0, -width * sine);
    const Vector v(0, height, 0);
    const Vector w(depth * sine, 0, depth * cosine);
    const Point origin = Point(center_x, 0, center_z) - 0.5 * (u + w);
    meshes.push_back(make_box<double>(origin, u, v, w, diffuse(white)));
  };
  cuboid(-1.2, -2.0, 1.5, 1.8, 1.6, -18.0);
  cuboid(1.1, -4.0, 1.6, 3.0, 1.7, 20.0);

  // These are faceted triangle meshes, not spheres with smooth normals.
  // The unsubdivided seed's lowest vertex is at -0.8506508 * radius;
  // levels 1+ have a vertex at -radius, so each mesh rests on its support.
  meshes.push_back(
      make_sphere<double>(Point(-2.5, 0.2 * 0.8506508083520399, -0.45),
                                0.2, 0, diffuse(Colour(0.85, 0.45, 0.08))));
  meshes.push_back(make_sphere<double>(
      Point(-0.2, 0.3, -0.55), 0.3, 3,
      std::make_unique<Metal<double>>(Colour(0.85, 0.65, 0.25))));
  meshes.push_back(make_sphere<double>(Point(1.25, 0.45, -1.0), 0.45, 2,
                                             diffuse(Colour(0.12, 0.3, 0.75))));
  meshes.push_back(make_sphere<double>(Point(-2.05, 0.65, -4.65), 0.65, 3,
                                             diffuse(Colour(0.45, 0.18, 0.6))));
  meshes.push_back(make_sphere<double>(
      Point(-1.2, 1.8 + 0.55, -2.0), 0.55, 6,
      std::make_unique<Metal<double>>(Colour(0.88, 0.9, 0.95))));

  Camera<double> camera;
  camera.aspect_ratio = 1.0;
  camera.image_width = 800;
  camera.samples_per_pixel = 600;
  camera.max_depth = 20;
  camera.vfov = 50;
  camera.look_from = Point(0, 3.2, 9);
  camera.look_at = Point(0, 2.5, -3);
  camera.v_up = Vector(0, 1, 0);

  return std::make_tuple(std::move(camera), std::move(meshes));
}
