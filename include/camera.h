#pragma once

#include <cassert>
#include <concepts>
#include <format>
#include <iostream>

#include <color.h>
#include <hittable.h>
#include <interval.h>
#include <utility.h>
#include <vec3.h>

template <std::floating_point T> class Camera {
public:
  T aspect_ratio = T{1.0};
  int image_width = 100;
  int samples_per_pixel = 10;
  int max_depth = 10; // Maximum number of ray bounces into scene

  T vfov = T{90}; // vertical view angle (field of view)
  point3<T> look_from =
      point3<T>(0, 0, 0); // the point the camera is looking from
  point3<T> look_at = point3<T>(0, 0, -1); // the point the camera is looking at
  vec3<T> v_up = vec3<T>(0, 1, 0); // relative up direction for the camera

  void render(const Hittable<T> &world) {
    initialize();

    // rendering
    std::cout << std::format("P3\n{} {}\n255\n", image_width, image_height);

    for (auto j = 0; j < image_height; j++) {

      std::clog << std::format("\rScanlines remaining: {} ", image_height - j);

      for (auto i = 0; i < image_width; i++) {

        Color<T> pixel_color(0, 0, 0);

        for (auto s = 0; s < samples_per_pixel; s += 1) {
          Ray r = get_ray(i, j);
          pixel_color += ray_color(r, max_depth, world);
        }

        write_color(std::cout, pixel_samples_scale * pixel_color);
      }
    }

    std::clog << "\rDone.                   \n";
  }

  [[nodiscard]] Color<T> render_checksum(const Hittable<T> &world,
                                         bool show_progress = false) {
    initialize();

    Color<T> checksum(0, 0, 0);

    for (int j = 0; j < image_height; ++j) {
      if (show_progress) {
        std::clog << "\rScanlines remaining: " << image_height - j << "     "
                  << std::flush;
      }

      for (int i = 0; i < image_width; ++i) {
        Color<T> pixel_color(0, 0, 0);

        for (int s = 0; s < samples_per_pixel; ++s) {
          const Ray<T> ray = get_ray(i, j);
          pixel_color += ray_color(ray, max_depth, world);
        }

        checksum += pixel_samples_scale * pixel_color;
      }
    }

    return checksum;
  }

private:
  int image_height;
  T pixel_samples_scale; // Color scale factor for a sum of pixel samples
  point3<T> camera_center;
  point3<T> pixel00; // Location of pixel 0, 0
  vec3<T> pixel_delta_u;
  vec3<T> pixel_delta_v;

  vec3<T> u, v, w; // camera frame basis vectors

  void initialize() noexcept {

    assert(aspect_ratio > T{0});
    assert(image_width > T{0});
    assert(samples_per_pixel > 0);

    image_height = int(image_width / aspect_ratio);
    image_height = (image_height < 1) ? 1 : image_height;

    pixel_samples_scale = T{1} / samples_per_pixel;

    camera_center = look_from;

    // set up the camera
    auto focal_length = (look_from - look_at).length();

    // determine viewport dims
    auto theta = degree_to_radians(vfov);
    auto h = std::tan(theta / 2);
    auto viewport_height = T{2.0} * h * focal_length;
    auto viewport_width =
        viewport_height *
        ((static_cast<T>(image_width) / static_cast<T>(image_height)));

    // calculate the camera frame basis vectors (camera coordinate frame)
    w = unit_vector(look_from - look_at);
    u = unit_vector(cross(v_up, w));
    v = cross(w, u);

    auto viewport_u =
        viewport_width * u; // vector across viewport horizontal edge
    auto viewport_v =
        viewport_height * -v; // vector across viewport vertical edge

    pixel_delta_u = viewport_u / image_width;
    pixel_delta_v = viewport_v / image_height;

    // calculate the location of the upper left pixel
    auto viewport_upper_left =
        camera_center - (focal_length * w) - viewport_u / 2 - viewport_v / 2;
    pixel00 = viewport_upper_left + pixel_delta_u / 2 + pixel_delta_v / 2;
  }

  [[nodiscard]] vec3<T> sample_square() const {
    return vec3<T>(random_real<T>() - T{0.5}, random_real<T>() - T{0.5}, 0);
  }

  [[nodiscard]] Ray<T> get_ray(int i, int j) const {
    // Construct a camera ray originating from the origin and directed at
    // randomly sampled point around the pixel location i, j.

    auto offset = sample_square();
    auto pixel_sample = pixel00 + ((i + offset.x()) * pixel_delta_u) +
                        ((j + offset.y()) * pixel_delta_v);

    auto ray_origin = camera_center;
    auto ray_direction = pixel_sample - ray_origin;
    T ray_time = random_real<T>();

    return Ray(ray_origin, ray_direction, ray_time);
  }

  [[nodiscard]] Color<T> ray_color(const Ray<T> &r, int depth,
                                   const Hittable<T> &world) const {

    // for recursive calls, if exceeded max depth, no more light is accumilated
    if (depth <= 0) {
      return Color<T>(0, 0, 0);
    }

    auto interval = Interval<T>::standard();

    // candidate hit record - candidate because it is an optional
    if (auto candidate_hit = world.hit(r, interval)) {

      // scatter candidate because some materials can absorb all of the light
      // (optional type)
      if (auto scatter_candidate =
              candidate_hit->material.get().scatter(r, candidate_hit.value())) {
        return scatter_candidate->attenuation *
               ray_color(scatter_candidate->scattered, depth - 1, world);
      }

      return Color<T>(0, 0, 0);

      // return 0.5 * (candidate->normal + Color<T>(1, 1, 1));
      // return 0.5 * ray_color(Ray(candidate->p, dir), depth - 1, world);
    }

    vec3<T> unit = unit_vector(r.direction());
    auto a = 0.5 * (unit.y() + 1.0); // remaps [-1, 1] to [0,1]
    return (1.0 - a) * Color(1.0, 1.0, 1.0) + a * Color(0.5, 0.7, 1.0);
  }
};
