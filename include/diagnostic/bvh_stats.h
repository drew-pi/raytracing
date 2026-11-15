#pragma once

#include <bvh.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace diagnostic {

template <std::floating_point T> struct BVHStatistics {
  std::size_t triangle_count = 0;
  std::size_t node_count = 0;
  std::size_t internal_node_count = 0;
  std::size_t leaf_count = 0;
  std::size_t maximum_depth = 0;
  double average_leaf_depth = 0;
  double average_primitives_per_leaf = 0;
  std::uint32_t maximum_primitives_in_leaf = 0;
  double estimated_sah_cost = 0;
  std::size_t node_bytes = 0;
  std::size_t index_bytes = 0;
};

template <std::floating_point T>
[[nodiscard]] BVHStatistics<T> bvh_statistics(const BVH<T> &bvh) {
  assert(!bvh.nodes.empty());

  BVHStatistics<T> result;
  result.triangle_count = bvh.indexes.size();
  result.node_count = bvh.nodes.size();
  result.node_bytes = bvh.nodes.capacity() * sizeof(BVHNode<T>);
  result.index_bytes = bvh.indexes.capacity() * sizeof(std::uint32_t);

  const T root_area = bvh.nodes.front().box.surface_area();
  assert(std::isfinite(root_area) && root_area > T{0});

  struct Entry {
    std::size_t index;
    std::size_t depth;
  };
  std::vector<Entry> pending{{0, 0}};
  std::size_t leaf_depth_sum = 0;
  std::size_t leaf_primitive_sum = 0;

  while (!pending.empty()) {
    const Entry entry = pending.back();
    pending.pop_back();
    const BVHNode<T> &node = bvh.nodes[entry.index];
    result.maximum_depth = std::max(result.maximum_depth, entry.depth);

    const double probability =
        static_cast<double>(node.box.surface_area() / root_area);
    if (node.primitive_count == 0) {
      ++result.internal_node_count;
      result.estimated_sah_cost +=
          probability * static_cast<double>(bvh.traversal_cost);
      pending.push_back({node.offset, entry.depth + 1});
      pending.push_back({entry.index + 1, entry.depth + 1});
    } else {
      ++result.leaf_count;
      leaf_depth_sum += entry.depth;
      leaf_primitive_sum += node.primitive_count;
      result.maximum_primitives_in_leaf =
          std::max(result.maximum_primitives_in_leaf, node.primitive_count);
      result.estimated_sah_cost += probability *
                                   static_cast<double>(bvh.intersection_cost) *
                                   static_cast<double>(node.primitive_count);
    }
  }

  assert(result.leaf_count > 0);
  result.average_leaf_depth =
      static_cast<double>(leaf_depth_sum) / result.leaf_count;
  result.average_primitives_per_leaf =
      static_cast<double>(leaf_primitive_sum) / result.leaf_count;
  return result;
}

} // namespace diagnostic
