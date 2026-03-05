#pragma once

#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

#include "gpu_common.h"

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