#include <format>
#include <iostream>
#include <memory>

#include <color.h>
#include <hittable.h>
#include <hittable_list.h>
#include <interval.h>
#include <ray.h>
#include <sphere.h>
#include <vec3.h>

color<double> ray_color(const Ray<double> &r, const Hittable<double> &world) {

  auto interval = Interval<double>();

  if (auto candidate = world.hit(r, interval)) {
    return 0.5 * (candidate->normal + color<double>(1, 1, 1));
  }

  vec3<double> unit = unit_vector(r.direction());
  auto a = 0.5 * (unit.y() + 1.0); // remaps [-1, 1] to [0,1]
  return (1.0 - a) * color(1.0, 1.0, 1.0) + a * color(0.5, 0.7, 1.0);
}

int main() {

  auto aspect_ratio = 16.0 / 9.0;
  auto image_width = 400;

  auto image_height = int(image_width / aspect_ratio);
  image_height = (image_height < 1) ? 1 : image_height;

  // set up the world
  HittableList<double> world;
  world.add(std::make_unique<Sphere<double>>(point3<double>(0, 0, -1), 0.5));
  world.add(
      std::make_unique<Sphere<double>>(point3<double>(0, -100.5, -1), 100));

  // set up the camera
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

      Ray r(camera_center, ray_direction);

      auto pixel_color = ray_color(r, world);
      write_color(std::cout, pixel_color);
    }
  }

  std::clog << "\rDone.                   \n";
}
