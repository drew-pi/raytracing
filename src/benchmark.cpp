#include "scene.h"
#include <bvh.h>

#include <chrono>
#include <iostream>
#include <utility>

int main() {
  rng::seed(12345, 0);

  using Clock = std::chrono::steady_clock;

  const auto t0 = Clock::now();
  auto [camera, world] = create_scene();

  const auto t1 = Clock::now();
  BVHNode<double> bvh(std::move(world));

  const auto t2 = Clock::now();
  const auto checksum = camera.render_checksum(bvh, false);
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
