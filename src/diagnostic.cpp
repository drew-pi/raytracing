#include "scenes/benchmark_scene.h"

#include <diagnostic/stats.h>
#include <scene.h>

#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

int main(int argc, char *argv[]) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
      std::cout
          << "Usage: " << argv[0]
          << " [--metadata | --help]\n"
             "Render the benchmark scene once with one diagnostic worker.\n"
             "Output is JSON Lines; timing is not a performance baseline.\n";
      return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--metadata") {
      write_benchmark_metadata(std::cout);
      return 0;
    }
    if (argc != 1)
      throw std::invalid_argument(
          "Expected no arguments, --metadata, or --help");

    auto [camera, meshes] = create_benchmark_scene();
    Scene<BenchmarkScalar> scene;
    scene.build(std::move(meshes));
    diagnostic::reset(camera.max_depth);

    const auto checksum = camera.render_checksum(scene, 1, false);
    if (!std::isfinite(checksum.x()) || !std::isfinite(checksum.y()) ||
        !std::isfinite(checksum.z()))
      throw std::runtime_error("Non-finite diagnostic checksum");

    std::cout << std::setprecision(17) << "{\"type\":\"render\",\"checksum\":["
              << checksum.x() << ',' << checksum.y() << ',' << checksum.z()
              << "]}\n";
    for (std::size_t depth = 0; depth < diagnostic::counters.size(); ++depth) {
      const auto &stats = diagnostic::counters[depth];
      std::cout << "{\"type\":\"depth\",\"depth\":" << depth
                << ",\"rays\":" << stats.rays
                << ",\"box_tests\":" << stats.box_tests
                << ",\"box_hits\":" << stats.box_hits
                << ",\"leaf_visits\":" << stats.leaf_visits
                << ",\"triangle_tests\":" << stats.triangle_tests
                << ",\"triangle_hits\":" << stats.triangle_hits << "}\n";
    }
    std::cout.flush();
    if (!std::cout)
      throw std::runtime_error("Cannot write diagnostic results");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Diagnostic render: " << error.what() << '\n';
    return 1;
  }
}
