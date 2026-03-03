#pragma once

#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

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

class MetalBeamSearch {
public:
  MetalBeamSearch(int max_n, int max_states, int max_rotations);
  ~MetalBeamSearch();

  void set_zobrist_table(const uint64_t *flat_zobrist_table);

  std::vector<GPUResult>
  evaluate_batch(const std::vector<uint16_t> &grids_flat, int num_states,
                 const std::vector<std::tuple<int, int, int>> &rotations,
                 int grid_size);

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
