#include "new_benchmark_scene.h"
#include "real_scene.h"
#include "triangle_test.h"

#include <scene.h>

#include <charconv>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <system_error>
#include <utility>

int main(int argc, char *argv[]) {
  int thread_count = 1;

  if (argc > 2) {
    std::cerr << "Usage: " << argv[0] << " [thread_count]\n";
    return 1;
  }

  if (argc == 2) {
    const std::string_view argument{argv[1]};

    const auto [end, error] = std::from_chars(
        argument.data(), argument.data() + argument.size(), thread_count);

    if (error != std::errc{} || end != argument.data() + argument.size() ||
        thread_count <= 0) {
      std::cerr << "Thread count must be a positive integer.\n";
      return 1;
    }
  }

  using Clock = std::chrono::steady_clock;

  const auto t0 = Clock::now();
  // auto [camera, meshes] = create_new_benchmark_scene();
  // auto [camera, meshes] = create_triangle_test_scene();
  auto [camera, meshes] = create_real_scene();
  const auto t1 = Clock::now();

  Scene<double> scene;
  scene.build(std::move(meshes));
  const auto t2 = Clock::now();

  const auto checksum = camera.render_checksum(scene, thread_count, false);
  const auto t3 = Clock::now();

  const auto milliseconds = [](auto begin, auto end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
  };

  std::cerr << std::setprecision(17) << "checksum: " << checksum.x() << ' '
            << checksum.y() << ' ' << checksum.z() << '\n';

  std::cerr << "\nscene_ms,build_ms,render_ms\n"
            << milliseconds(t0, t1) << ',' << milliseconds(t1, t2) << ','
            << milliseconds(t2, t3) << '\n';

  return 0;
}
