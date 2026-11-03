#include "scenes/benchmark_scene.h"

#include <scene.h>

#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

struct Options {
  int runs = 5;
  int threads = benchmark_thread_count;
  bool help = false;
  bool metadata = false;
};

int positive_integer(std::string_view value, std::string_view option) {
  int result = 0;
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (error != std::errc{} || end != value.data() + value.size() ||
      result <= 0) {
    throw std::invalid_argument(std::string(option) +
                                " must be a positive integer in int range");
  }
  return result;
}

Options parse_options(int argc, char *argv[]) {
  Options options;
  bool runs_seen = false;
  bool threads_seen = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view option{argv[i]};
    if (option == "--help") {
      options.help = true;
      continue;
    }
    if (option == "--metadata") {
      options.metadata = true;
      continue;
    }
    if (option != "--runs" && option != "--threads") {
      throw std::invalid_argument("Unknown argument: " + std::string(option));
    }
    if (i + 1 == argc || std::string_view(argv[i + 1]).starts_with("--") ||
        std::string_view(argv[i + 1]).empty()) {
      throw std::invalid_argument("Missing value for " + std::string(option));
    }
    const std::string_view value{argv[++i]};
    if (option == "--runs") {
      if (runs_seen) {
        throw std::invalid_argument("Duplicate --runs");
      }
      runs_seen = true;
      options.runs = positive_integer(value, option);
    } else {
      if (threads_seen) {
        throw std::invalid_argument("Duplicate --threads");
      }
      threads_seen = true;
      options.threads = positive_integer(value, option);
    }
  }
  if (options.metadata && (runs_seen || threads_seen)) {
    throw std::invalid_argument(
        "--metadata cannot be combined with --runs or --threads");
  }
  return options;
}

void require_finite(double value, std::string_view name) {
  if (!std::isfinite(value)) {
    throw std::runtime_error("Non-finite " + std::string(name));
  }
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    const auto options = parse_options(argc, argv);
    if (options.help) {
      std::cout << "Usage: " << argv[0]
                << " [--runs N] [--threads N] [--metadata] [--help]\n"
                   "  --runs N       Positive trial count (default: 5)\n"
                   "  --threads N    Positive worker count (default: "
                << benchmark_thread_count
                << ")\n"
                   "  --metadata     Print scene JSON without loading assets "
                   "or rendering\n";
      return 0;
    }

    if (options.metadata) {
      write_benchmark_metadata(std::cout);
      return 0;
    }

    using Clock = std::chrono::steady_clock;
    const auto milliseconds = [](auto begin, auto end) {
      return std::chrono::duration<double, std::milli>(end - begin).count();
    };

    const auto scene_begin = Clock::now();
    auto [camera, meshes] = create_benchmark_scene();
    const auto scene_end = Clock::now();
    Scene<double> scene;
    scene.build(std::move(meshes));
    const auto build_end = Clock::now();

    const double scene_ms = milliseconds(scene_begin, scene_end);
    const double build_ms = milliseconds(scene_end, build_end);
    require_finite(scene_ms, "scene_ms");
    require_finite(build_ms, "build_ms");
    std::cout << std::setprecision(17)
              << "{\"type\":\"startup\",\"scene_ms\":" << scene_ms
              << ",\"build_ms\":" << build_ms << "}\n"
              << std::flush;
    if (!std::cout) {
      throw std::runtime_error("Cannot write startup JSON");
    }

    for (int trial = 0; trial < options.runs; ++trial) {
      const auto render_begin = Clock::now();
      const auto checksum =
          camera.render_checksum(scene, options.threads, false);
      const auto render_end = Clock::now();

      const double render_ms = milliseconds(render_begin, render_end);
      require_finite(render_ms, "render_ms");
      require_finite(checksum.x(), "checksum red");
      require_finite(checksum.y(), "checksum green");
      require_finite(checksum.z(), "checksum blue");
      std::cout << "{\"type\":\"render\",\"run\":" << trial + 1
                << ",\"render_ms\":" << render_ms << ",\"checksum\":["
                << checksum.x() << ',' << checksum.y() << ',' << checksum.z()
                << "]}\n"
                << std::flush;
      if (!std::cout) {
        throw std::runtime_error("Cannot write render JSON");
      }
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Rendering benchmark: " << error.what() << '\n';
    return 1;
  }
}
