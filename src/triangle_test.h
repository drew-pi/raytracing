#pragma once

#include <cmath>
#include <memory>
#include <numbers>
#include <tuple>
#include <utility>

#include <camera.h>
#include <color.h>
#include <hittable_list.h>
#include <material.h>
#include <triangle.h>
#include <vec3.h>

[[nodiscard]] inline std::tuple<Camera<double>, HittableList<double>>
create_triangle_test_scene() {
  using Point = point3<double>;
  using Colour = Color<double>;

  HittableList<double> world;

  const Colour white(0.73, 0.73, 0.73);
  const Colour red(0.65, 0.05, 0.05);
  const Colour green(0.12, 0.45, 0.15);

  auto triangle = [&](const Point &a, const Point &b, const Point &c,
                      const Colour &color) {
    world.add(std::make_unique<Triangle<double>>(
        a, b, c, std::make_unique<Lambertian<double>>(color)));
  };

  // Vertices follow the desired normal; both triangles share diagonal a-c.
  auto quad = [&](const Point &a, const Point &b, const Point &c,
                  const Point &d, const Colour &color) {
    triangle(a, b, c, color);
    triangle(a, c, d, color);
  };

  // Interior: x in [-3, 3], y in [0, 6], z in [-6, 0].
  // Enclosure normals point inward. Front and ceiling remain open to the sky.
  quad(Point(-3, 0, 0), Point(3, 0, 0), Point(3, 0, -6), Point(-3, 0, -6),
       white); // Floor: +Y

  quad(Point(-3, 0, -6), Point(3, 0, -6), Point(3, 6, -6), Point(-3, 6, -6),
       white); // Back: +Z

  quad(Point(-3, 0, 0), Point(-3, 0, -6), Point(-3, 6, -6), Point(-3, 6, 0),
       red); // Left: +X

  quad(Point(3, 0, -6), Point(3, 0, 0), Point(3, 6, 0), Point(3, 6, -6),
       green); // Right: -X

  auto cuboid = [&](double center_x, double center_z, double width,
                    double height, double depth, double angle_degrees) {
    const double angle = angle_degrees * std::numbers::pi_v<double> / 180.0;
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);

    // Rotate about Y around the center of the cuboid's footprint.
    auto vertex = [&](double x, double y, double z) {
      return Point(center_x + cosine * x + sine * z, y,
                   center_z - sine * x + cosine * z);
    };

    const double x = width / 2.0;
    const double z = depth / 2.0;

    const Point p0 = vertex(-x, 0, -z);
    const Point p1 = vertex(x, 0, -z);
    const Point p2 = vertex(x, 0, z);
    const Point p3 = vertex(-x, 0, z);
    const Point p4 = vertex(-x, height, -z);
    const Point p5 = vertex(x, height, -z);
    const Point p6 = vertex(x, height, z);
    const Point p7 = vertex(-x, height, z);

    // Outward winding on every face, including the bottom.
    quad(p0, p1, p2, p3, white); // Bottom
    quad(p4, p7, p6, p5, white); // Top
    quad(p0, p4, p5, p1, white); // Local -Z
    quad(p3, p2, p6, p7, white); // Local +Z
    quad(p0, p3, p7, p4, white); // Local -X
    quad(p1, p5, p6, p2, white); // Local +X
  };

  cuboid(-1.2, -2.0, 1.5, 1.8, 1.6, -18.0);
  cuboid(1.1, -4.0, 1.6, 3.0, 1.7, 20.0);

  Camera<double> camera;
  camera.aspect_ratio = 1.0;
  camera.image_width = 800;
  camera.samples_per_pixel = 600;
  camera.max_depth = 20;
  camera.vfov = 50;
  camera.look_from = Point(0, 3.2, 9);
  camera.look_at = Point(0, 2.5, -3);
  camera.v_up = vec3<double>(0, 1, 0);

  return std::make_tuple(std::move(camera), std::move(world));
}
