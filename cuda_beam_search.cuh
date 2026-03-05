#pragma once

#include "gpu_common.h"
#include <cstdint>
#include <tuple>
#include <vector>

class CudaBeamSearch {
public:
  CudaBeamSearch(int max_n, int max_states, int max_rotations);
  ~CudaBeamSearch();

  void set_zobrist_table(const uint64_t *flat_zobrist_table);

  std::vector<GPUResult>
  evaluate_batch(const std::vector<uint16_t> &grids_flat, int num_states,
                 const std::vector<std::tuple<int, int, int>> &rotations,
                 int grid_size);

private:
  int max_n_;
  int max_states_;
  int max_rotations_;

  uint16_t *d_grids_;
  int *d_rotations_;
  uint64_t *d_zobrist_;
  GPUResult *d_results_;
};