#pragma once

#include <cstdint>

#ifndef GPU_RESULT_DEFINED
#define GPU_RESULT_DEFINED
struct GPUResult {
  int paired_count;
  int heuristic;
  uint64_t hash;
  int parent_idx;
  int rotation_idx;

  bool operator<(const GPUResult &other) const {
    if (paired_count != other.paired_count)
      return paired_count < other.paired_count;
    return heuristic > other.heuristic;
  }
};
#endif
