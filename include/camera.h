#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <concepts>
#include <format>
#include <iostream>
#include <thread>
#include <vector>

#include <color.h>
#include <hittable.h>
#include <interval.h>
#include <scene.h>
#include <utility.h>
#include <vec3.h>

template <std::floating_point T> class Camera {
public:
  T aspect_ratio = T{1.0};
  int image_width = 100;
  int samples_per_pixel = 10;
  int max_depth = 10; // Maximum number of ray bounces into scene
  int tile_size = 32;

  T vfov = T{90}; // vertical view angle (field of view)
  point3<T> look_from =
      point3<T>(0, 0, 0); // the point the camera is looking from
  point3<T> look_at = point3<T>(0, 0, -1); // the point the camera is looking at
  vec3<T> v_up = vec3<T>(0, 1, 0); // relative up direction for the camera

  // turns a world pixel coordinate into a vector array coordinate
  size_t get_pixel(const int i, const int j) {
    return static_cast<size_t>(j) * image_width + i;
  }

  void tile_renderer(const Scene<T> &scene) {
    assert(tile_size > 0);
    initialize();

    const int tile_columns = (image_width + tile_size - 1) / tile_size;
    const int tile_rows = (image_height + tile_size - 1) / tile_size;

    std::vector<Color<T>> buffer(static_cast<size_t>(image_width) *
                                 static_cast<size_t>(image_height));

    for (int j = 0; j < tile_rows; ++j) {
      for (int i = 0; i < tile_columns; ++i) {
        std::clog << "\rTiles remaining: "
                  << (static_cast<size_t>(tile_rows - j) * tile_columns - i)
                  << "     " << std::flush;

        render_tile(i, j, scene, buffer);
      }
    }

    std::cout << std::format("P3\n{} {}\n255\n", image_width, image_height);

    for (const auto &pixel : buffer)
      write_color(std::cout, pixel);

    std::clog << "\rDone.                   \n";
  }

  void render_multi_thread(const Scene<T> &scene, const int thread_count) {
    assert(thread_count > 0);

    initialize();

    const int tile_columns = (image_width + tile_size - 1) / tile_size;
    const int tile_rows = (image_height + tile_size - 1) / tile_size;

    std::vector<Color<T>> buffer(static_cast<size_t>(image_width) *
                                 static_cast<size_t>(image_height));

    std::vector<std::jthread> workers;
    workers.reserve(thread_count);

    std::atomic<size_t> next_tile{0};
    std::atomic<size_t> completed_tiles{0};

    const size_t total_tiles = static_cast<size_t>(tile_columns) * tile_rows;

    // thread work
    auto worker = [&](size_t worker_id) {
      rng::seed(12345, worker_id);

      while (true) {
        // Claim a unique tile.
        const size_t tile = next_tile.fetch_add(1, std::memory_order_relaxed);

        if (tile >= total_tiles)
          break;

        const int column = static_cast<int>(tile % tile_columns);
        const int row = static_cast<int>(tile / tile_columns);

        render_tile(column, row, scene, buffer);
        completed_tiles.fetch_add(1, std::memory_order_relaxed);
      }
    };

    for (size_t worker_id = 0; worker_id < thread_count; ++worker_id)
      workers.emplace_back(worker, worker_id);

    // main thread polling
    while (true) {
      const size_t completed = completed_tiles.load(std::memory_order_relaxed);

      std::clog << "\rTiles remaining: " << total_tiles - completed << '/'
                << total_tiles << "     " << std::flush;

      if (completed == total_tiles)
        break;

      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // join threads before rendering
    for (auto &thread : workers)
      thread.join();

    // rendering
    std::cout << std::format("P3\n{} {}\n255\n", image_width, image_height);

    for (const auto &pixel : buffer)
      write_color(std::cout, pixel);

    std::clog << "\rDone.                   \n";
  }

  [[nodiscard]] Color<T> render_checksum(const Scene<T> &scene,
                                         const int thread_count,
                                         bool show_progress = false) {
    assert(thread_count > 0);

    initialize();

    const int tile_columns = (image_width + tile_size - 1) / tile_size;
    const int tile_rows = (image_height + tile_size - 1) / tile_size;

    std::vector<Color<T>> buffer(static_cast<size_t>(image_width) *
                                 static_cast<size_t>(image_height));

    std::vector<std::jthread> workers;
    workers.reserve(thread_count);

    std::atomic<size_t> next_tile{0};
    std::atomic<size_t> completed_tiles{0};

    const size_t total_tiles = static_cast<size_t>(tile_columns) * tile_rows;

    // thread work
    auto worker = [&](size_t worker_id) {
      rng::seed(12345, worker_id);

      while (true) {
        // Claim a unique tile.
        const size_t tile = next_tile.fetch_add(1, std::memory_order_relaxed);

        if (tile >= total_tiles)
          break;

        const int column = static_cast<int>(tile % tile_columns);
        const int row = static_cast<int>(tile / tile_columns);

        render_tile(column, row, scene, buffer);
        completed_tiles.fetch_add(1, std::memory_order_relaxed);
      }
    };

    for (size_t worker_id = 0; worker_id < thread_count; ++worker_id)
      workers.emplace_back(worker, worker_id);

    // main thread polling if turned on
    if (show_progress) {
      while (true) {
        const size_t completed =
            completed_tiles.load(std::memory_order_relaxed);

        std::clog << "\rTiles remaining: " << total_tiles - completed << '/'
                  << total_tiles << "     " << std::flush;

        if (completed == total_tiles)
          break;

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    }

    // join threads before rendering
    for (auto &thread : workers)
      thread.join();

    // checksum
    Color<T> checksum(0, 0, 0);

    for (const auto &pixel : buffer)
      checksum += pixel;

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

  void render_tile(const int column, const int row, const Scene<T> &scene,
                   std::vector<Color<T>> &buffer) {

    const int x_begin = column * tile_size;
    const int y_begin = row * tile_size;

    const int x_end = std::min(x_begin + tile_size, image_width);
    const int y_end = std::min(y_begin + tile_size, image_height);

    assert(x_begin >= 0 && x_begin < image_width);
    assert(y_begin >= 0 && y_begin < image_height);

    for (auto j = y_begin; j < y_end; ++j) {

      for (auto i = x_begin; i < x_end; i++) {

        Color<T> pixel_color(0, 0, 0);

        for (auto s = 0; s < samples_per_pixel; s += 1) {
          Ray r = get_ray(i, j);
          pixel_color += ray_color(r, max_depth, scene);
        }

        buffer[get_pixel(i, j)] = pixel_samples_scale * pixel_color;
      }
    }
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
                                   const Scene<T> &scene) const {

    // for recursive calls, if exceeded max depth, no more light is accumilated
    if (depth <= 0) {
      return Color<T>(0, 0, 0);
    }

    auto interval = Interval<T>::standard();

    // candidate hit record - candidate because it is an optional
    if (auto candidate_hit = scene.hit(r, interval)) {

      // scatter candidate because some materials can absorb all of the light
      // (optional type)
      if (auto scatter_candidate =
              candidate_hit->material.get().scatter(r, candidate_hit.value())) {
        return scatter_candidate->attenuation *
               ray_color(scatter_candidate->scattered, depth - 1, scene);
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
