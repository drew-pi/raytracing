#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

struct DepthCounters {
  std::uint64_t rays = 0;
  std::uint64_t box_tests = 0;
  std::uint64_t box_hits = 0;
  std::uint64_t leaf_visits = 0;
  std::uint64_t triangle_tests = 0;
  std::uint64_t triangle_hits = 0;
};

namespace diagnostic {

// Single-worker storage: initialize before rendering and read only after join.
inline std::vector<DepthCounters> counters;
inline DepthCounters *active_counters = nullptr;

inline void reset(int depth_count) {
  if (depth_count <= 0)
    throw std::invalid_argument("Diagnostic maximum depth must be positive");
  counters.assign(static_cast<std::size_t>(depth_count), DepthCounters{});
  active_counters = nullptr;
}

class QueryScope {
public:
  explicit QueryScope(int bounce) : previous(active_counters) {
    active_counters = &counters.at(static_cast<std::size_t>(bounce));
    ++active_counters->rays;
  }

  ~QueryScope() { active_counters = previous; }

  QueryScope(const QueryScope &) = delete;
  QueryScope &operator=(const QueryScope &) = delete;

private:
  DepthCounters *previous;
};

} // namespace diagnostic
