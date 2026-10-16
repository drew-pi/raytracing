#include "scene.h"
#include <bvh.h>

#include <charconv>
#include <chrono>
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
  auto [camera, world] = create_scene();

  const auto t1 = Clock::now();
  BVHNode<double> bvh(std::move(world));

  const auto t2 = Clock::now();
  const auto checksum = camera.render_checksum(bvh, thread_count, false);
  // camera.render(bvh);
  const auto t3 = Clock::now();

  std::cerr << "checksum: " << checksum.x() << ' ' << checksum.y() << ' '
            << checksum.z() << '\n';

  const auto milliseconds = [](auto begin, auto end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
  };

  std::cerr << "\nscene_ms,build_ms,render_ms\n"
            << milliseconds(t0, t1) << ',' << milliseconds(t1, t2) << ','
            << milliseconds(t2, t3) << '\n';
}
