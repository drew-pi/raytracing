#include "scenes/benchmark_scene.h"

#include <bvh.h>
#include <diagnostic/bvh_stats.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Geometry {
  std::vector<point3<BenchmarkScalar>> positions;
  std::vector<SceneTriangle> triangles;
};

struct Options {
  int runs = 3;
  std::vector<std::size_t> sizes{1000, 10000, 100000, 1000000};
  bool metadata = false;
  bool help = false;
};

[[nodiscard]] std::size_t parse_size(std::string_view text, const char *name) {
  std::size_t value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value == 0 ||
      value >= std::numeric_limits<std::uint32_t>::max())
    throw std::invalid_argument(std::string(name) +
                                " must contain positive values below 2^32");
  return value;
}

[[nodiscard]] Options parse_options(int argc, char *argv[]) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument(argv[i]);
    if (argument == "--help") {
      options.help = true;
    } else if (argument == "--metadata") {
      options.metadata = true;
    } else if (argument == "--runs" && i + 1 < argc) {
      const std::size_t value = parse_size(argv[++i], "--runs");
      if (value > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("--runs is too large");
      options.runs = static_cast<int>(value);
    } else if (argument == "--sizes" && i + 1 < argc) {
      options.sizes.clear();
      std::string_view values(argv[++i]);
      while (!values.empty()) {
        const std::size_t comma = values.find(',');
        const std::string_view item = values.substr(0, comma);
        options.sizes.push_back(parse_size(item, "--sizes"));
        if (comma == std::string_view::npos)
          break;
        values.remove_prefix(comma + 1);
      }
      std::sort(options.sizes.begin(), options.sizes.end());
      if (std::adjacent_find(options.sizes.begin(), options.sizes.end()) !=
          options.sizes.end())
        throw std::invalid_argument("--sizes must not contain duplicates");
    } else {
      throw std::invalid_argument("Unknown or incomplete option: " +
                                  std::string(argument));
    }
  }
  if ((options.help || options.metadata) && argc != 2)
    throw std::invalid_argument("--help and --metadata must be used alone");
  return options;
}

void append_triangle(Geometry &geometry, const point3<BenchmarkScalar> &a,
                     const point3<BenchmarkScalar> &b,
                     const point3<BenchmarkScalar> &c) {
  if (geometry.positions.size() >
      std::numeric_limits<std::uint32_t>::max() - std::size_t{3})
    throw std::length_error(
        "Construction geometry exceeds 32-bit vertex indices");
  const auto first = static_cast<std::uint32_t>(geometry.positions.size());
  geometry.positions.push_back(a);
  geometry.positions.push_back(b);
  geometry.positions.push_back(c);
  geometry.triangles.push_back({{first, first + 1, first + 2}, 0});
}

[[nodiscard]] point3<BenchmarkScalar> lattice_point(std::size_t index,
                                                    std::size_t count) {
  const auto side = static_cast<std::size_t>(
      std::ceil(std::cbrt(static_cast<BenchmarkScalar>(count))));
  const std::size_t x = index % side;
  const std::size_t y = (index / side) % side;
  const std::size_t z = index / (side * side);
  return {static_cast<BenchmarkScalar>(x), static_cast<BenchmarkScalar>(y),
          static_cast<BenchmarkScalar>(z)};
}

[[nodiscard]] Geometry make_uniform(std::size_t count) {
  Geometry geometry;
  geometry.positions.reserve(count * 3);
  geometry.triangles.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto center = 2.0 * lattice_point(i, count);
    append_triangle(geometry, center + vec3<BenchmarkScalar>(-0.35, -0.25, 0.0),
                    center + vec3<BenchmarkScalar>(0.4, -0.2, 0.1),
                    center + vec3<BenchmarkScalar>(0.0, 0.45, -0.1));
  }
  return geometry;
}

[[nodiscard]] Geometry make_clustered(std::size_t count) {
  Geometry geometry;
  geometry.positions.reserve(count * 3);
  geometry.triangles.reserve(count);
  const std::size_t per_cluster = (count + 7) / 8;
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t cluster = i % 8;
    const std::size_t local_index = i / std::size_t{8};
    const auto local = 0.12 * lattice_point(local_index, per_cluster);
    const point3<BenchmarkScalar> origin((cluster & 1) ? 30.0 : -30.0,
                                         (cluster & 2) ? 30.0 : -30.0,
                                         (cluster & 4) ? 30.0 : -30.0);
    const auto center = origin + local;
    append_triangle(geometry, center + vec3<BenchmarkScalar>(-0.04, -0.03, 0.0),
                    center + vec3<BenchmarkScalar>(0.05, -0.02, 0.01),
                    center + vec3<BenchmarkScalar>(0.0, 0.05, -0.01));
  }
  return geometry;
}

[[nodiscard]] Geometry make_coplanar(std::size_t count) {
  Geometry geometry;
  geometry.positions.reserve(count * 3);
  geometry.triangles.reserve(count);
  const auto side = static_cast<std::size_t>(
      std::ceil(std::sqrt(static_cast<BenchmarkScalar>(count))));
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t row = i / side;
    const BenchmarkScalar x = static_cast<BenchmarkScalar>(i % side);
    const BenchmarkScalar y = static_cast<BenchmarkScalar>(row);
    append_triangle(geometry, {x, y, 0.0f}, {x + 0.9f, y, 0.0f},
                    {x, y + 0.9f, 0.0f});
  }
  return geometry;
}

[[nodiscard]] Geometry make_identical_centroids(std::size_t count) {
  Geometry geometry;
  geometry.positions.reserve(count * 3);
  geometry.triangles.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const BenchmarkScalar radius =
        1.0 + static_cast<BenchmarkScalar>(i % 31) / 31.0;
    const BenchmarkScalar y = 0.5 + static_cast<BenchmarkScalar>(i % 17) / 34.0;
    const BenchmarkScalar z =
        0.25 + static_cast<BenchmarkScalar>(i % 13) / 52.0;
    append_triangle(geometry, {radius, 0.0f, 0.0f}, {-0.5f * radius, y, z},
                    {-0.5f * radius, -y, -z});
  }
  return geometry;
}

[[nodiscard]] Geometry make_thin(std::size_t count) {
  Geometry geometry;
  geometry.positions.reserve(count * 3);
  geometry.triangles.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto center = 1.5 * lattice_point(i, count);
    append_triangle(geometry, center + vec3<BenchmarkScalar>(-0.7, 0.0, 0.0),
                    center + vec3<BenchmarkScalar>(0.7, 0.0, 0.0),
                    center + vec3<BenchmarkScalar>(0.0, 1e-6, 1e-6));
  }
  return geometry;
}

[[nodiscard]] Geometry make_dragon() {
  auto [camera, meshes] = create_benchmark_scene();
  (void)camera;
  Geometry geometry;
  std::size_t vertices = 0;
  std::size_t triangles = 0;
  for (const auto &mesh : meshes) {
    vertices += mesh.vertex_count();
    triangles += mesh.triangle_count();
  }
  geometry.positions.reserve(vertices);
  geometry.triangles.reserve(triangles);
  for (const auto &mesh : meshes) {
    const auto offset = static_cast<std::uint32_t>(geometry.positions.size());
    mesh.append_positions(geometry.positions);
    mesh.append_triangles(geometry.triangles, offset, 0);
  }
  return geometry;
}

void write_case(std::string_view scene, std::string_view category,
                std::optional<std::size_t> requested, const Geometry &geometry,
                int runs) {
  using Clock = std::chrono::steady_clock;
  std::vector<BenchmarkScalar> times;
  times.reserve(static_cast<std::size_t>(runs));
  diagnostic::BVHStatistics<BenchmarkScalar> expected;

  for (int run = 0; run < runs; ++run) {
    const auto begin = Clock::now();
    BVH<BenchmarkScalar> bvh(geometry.positions, geometry.triangles);
    const auto end = Clock::now();
    times.push_back(
        std::chrono::duration<BenchmarkScalar, std::milli>(end - begin)
            .count());
    const auto stats = diagnostic::bvh_statistics(bvh);
    if (run == 0) {
      expected = stats;
    } else if (stats.node_count != expected.node_count ||
               stats.leaf_count != expected.leaf_count ||
               stats.estimated_sah_cost != expected.estimated_sah_cost) {
      throw std::runtime_error("BVH construction is not deterministic");
    }
  }

  std::cout << std::setprecision(17)
            << "{\"type\":\"construction\",\"scene\":\"" << scene
            << "\",\"category\":\"" << category
            << "\",\"requested_triangles\":";
  if (requested)
    std::cout << *requested;
  else
    std::cout << "null";
  std::cout << ",\"triangle_count\":" << expected.triangle_count
            << ",\"runs\":" << runs << ",\"build_times_ms\":[";
  for (std::size_t i = 0; i < times.size(); ++i) {
    if (i)
      std::cout << ',';
    std::cout << times[i];
  }
  std::cout << "]"
            << ",\"node_count\":" << expected.node_count
            << ",\"internal_node_count\":" << expected.internal_node_count
            << ",\"leaf_count\":" << expected.leaf_count
            << ",\"maximum_depth\":" << expected.maximum_depth
            << ",\"average_leaf_depth\":" << expected.average_leaf_depth
            << ",\"average_primitives_per_leaf\":"
            << expected.average_primitives_per_leaf
            << ",\"maximum_primitives_in_leaf\":"
            << expected.maximum_primitives_in_leaf
            << ",\"estimated_sah_cost\":" << expected.estimated_sah_cost
            << ",\"node_bytes\":" << expected.node_bytes
            << ",\"index_bytes\":" << expected.index_bytes << "}\n"
            << std::flush;
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    const Options options = parse_options(argc, argv);
    if (options.help) {
      std::cout << "Usage: " << argv[0]
                << " [--runs N] [--sizes N,N,...] | --metadata | --help\n";
      return 0;
    }
    if (options.metadata) {
      std::cout << "{\"suite\":\"bvh-construction-v1\",\"timed_scope\":"
                   "\"BVH construction from prepared geometry\","
                   "\"sah_traversal_cost\":1,\"sah_intersection_cost\":0.5,"
                   "\"memory_measurement\":\"final vector capacity bytes; not "
                   "peak RSS\"}\n";
      return 0;
    }

    for (const std::size_t size : options.sizes) {
      write_case("uniform", "uniform", size, make_uniform(size), options.runs);
      write_case("clustered", "clustered", size, make_clustered(size),
                 options.runs);
      write_case("coplanar", "adversarial", size, make_coplanar(size),
                 options.runs);
      write_case("identical-centroids", "adversarial", size,
                 make_identical_centroids(size), options.runs);
      write_case("long-thin", "adversarial", size, make_thin(size),
                 options.runs);
    }
    write_case("dragon", "nonuniform", std::nullopt, make_dragon(),
               options.runs);
    std::cout << "{\"type\":\"complete\"}\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Construction analysis: " << error.what() << '\n';
    return 1;
  }
}
