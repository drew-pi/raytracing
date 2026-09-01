#include <format>
#include <iostream>

#include <color.h>
#include <ray.h>
#include <vec3.h>

bool hit_sphere(const point3<double> &center, double radius,
                const ray<double> &r) {

  auto oc = center - r.origin();
  auto a = dot(r.direction(), r.direction());
  auto b = -2 * dot(r.direction(), oc);
  auto c = dot(oc, oc) - radius * radius;

  auto discriminant = b * b - 4 * a * c;
  return (discriminant >= 0);
}

auto ray_color(const ray<double> &r) {
  auto circle_center = point3<double>(0, 0, -2);
  auto circle_radius = 0.5;

  if (hit_sphere(circle_center, circle_radius, r))
    return color<double>(1, 0, 0);

  vec3<double> unit = unit_vector(r.direction());
  auto a = 0.5 * (unit.y() + 1.0); // remaps [-1, 1] to [0,1]
  return (1.0 - a) * color(1.0, 1.0, 1.0) + a * color(0.5, 0.7, 1.0);
}

int main() {

  auto aspect_ratio = 16.0 / 9.0;
  auto image_width = 400;

  auto image_height = int(image_width / aspect_ratio);
  image_height = (image_height < 1) ? 1 : image_height;

  auto focal_length = 1.0;

  auto viewport_height = 2.0;
  auto viewport_width = viewport_height * (double(image_width) / image_height);

  auto camera_center = point3<double>(0, 0, 0);

  auto viewport_u = vec3<double>(viewport_width, 0, 0);
  auto viewport_v = vec3<double>(0, -viewport_height, 0);

  auto pixel_delta_u = viewport_u / image_width;
  auto pixel_delta_v = viewport_v / image_height;

  // calculate the location of the upper left pixel
  auto viewport_upper_left = camera_center - vec3<double>(0, 0, focal_length) -
                             viewport_u / 2 - viewport_v / 2;
  auto pixel00 = viewport_upper_left + pixel_delta_u / 2 + pixel_delta_v / 2;

  // rendering

  std::cout << std::format("P3\n{} {}\n255\n", image_width, image_height);

  for (auto j = 0; j < image_height; j++) {

    std::clog << std::format("\rScanlines remaining: {} ", image_height - j);

    for (auto i = 0; i < image_width; i++) {

      auto pixel_center = pixel00 + pixel_delta_u * i + pixel_delta_v * j;
      auto ray_direction = pixel_center - camera_center;

      ray r(camera_center, ray_direction);

      auto pixel_color = ray_color(r);
      write_color(std::cout, pixel_color);
    }
  }

  std::clog << "\rDone.                   \n";
}
