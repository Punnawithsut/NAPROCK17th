#include "cuda_beam_search.cuh"
#include <cmath>
#include <cuda_runtime.h>
#include <iostream>

#define CHECK_CUDA(call)                                                       \
  do {                                                                         \
    cudaError_t err = call;                                                    \
    if (err != cudaSuccess) {                                                  \
      std::cerr << "CUDA error at " << __FILE__ << ":" << __LINE__             \
                << " code=" << err << " \"" << cudaGetErrorString(err) << "\"" \
                << std::endl;                                                  \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

struct Rotation {
  int k, i, j;
};

// Based on the Apple Metal shader but adapted for NVIDIA CUDA
__global__ void evaluate_rotations_kernel(
    const uint16_t *__restrict__ grids, const Rotation *__restrict__ rotations,
    const uint64_t *__restrict__ zobrist_table, GPUResult *__restrict__ results,
    int grid_size, int num_states, int num_rotations) {
  int id = blockIdx.x * blockDim.x + threadIdx.x;
  if (id >= num_states * num_rotations)
    return;

  int state_idx = id / num_rotations;
  int rot_idx = id % num_rotations;

  const uint16_t *original_grid = grids + (state_idx * grid_size * grid_size);
  Rotation rot = rotations[rot_idx];

  // Read to thread-local memory
  uint16_t local_grid[4096];
  for (int r = 0; r < grid_size; r++) {
    for (int c = 0; c < grid_size; c++) {
      local_grid[r * grid_size + c] = original_grid[r * grid_size + c];
    }
  }

  // Apply rotation
  int k = rot.k;
  int i = rot.i;
  int j = rot.j;

  uint16_t temp[4096];
  for (int x = 0; x < k; x++) {
    for (int y = 0; y < k; y++) {
      temp[y * k + (k - 1 - x)] = local_grid[(i + x) * grid_size + (j + y)];
    }
  }

  for (int x = 0; x < k; x++) {
    for (int y = 0; y < k; y++) {
      local_grid[(i + x) * grid_size + (j + y)] = temp[x * k + y];
    }
  }

  // Accumulate values
  int max_val = 0;
  uint64_t hash = 0;
  for (int r = 0; r < grid_size; r++) {
    for (int c = 0; c < grid_size; c++) {
      int val = local_grid[r * grid_size + c];
      if (val > max_val)
        max_val = val;

      // ZOBRIST_MAX_N=64, ZOBRIST_MAX_VAL=2048
      int z_idx = (r * 64 * 2048) + (c * 2048) + val;
      hash ^= zobrist_table[z_idx];
    }
  }

  // Pair count logic
  struct Coord {
    int r, c;
  };
  Coord coords1[2048];
  Coord coords2[2048];
  int coord_counts[2048];

  for (int v = 0; v <= max_val; ++v) {
    coord_counts[v] = 0;
  }

  for (int r = 0; r < grid_size; r++) {
    for (int c = 0; c < grid_size; c++) {
      int val = local_grid[r * grid_size + c];
      if (coord_counts[val] == 0) {
        coords1[val].r = r;
        coords1[val].c = c;
        coord_counts[val] = 1;
      } else {
        coords2[val].r = r;
        coords2[val].c = c;
        coord_counts[val] = 2;
      }
    }
  }

  int paired_count = 0;
  int total_dist = 0;

  for (int v = 0; v <= max_val; ++v) {
    if (coord_counts[v] == 2) {
      Coord p1 = coords1[v];
      Coord p2 = coords2[v];

      int dr = abs(p1.r - p2.r);
      int dc = abs(p1.c - p2.c);
      int dist = dr + dc;

      if (dist == 1) {
        paired_count++;
      }

      total_dist += dist + max((int)dr, (int)dc);
    }
  }

  results[id].paired_count = paired_count;
  results[id].heuristic = total_dist;
  results[id].hash = hash;
  results[id].parent_idx = state_idx;
  results[id].rotation_idx = rot_idx;
}

CudaBeamSearch::CudaBeamSearch(int max_n, int max_states, int max_rotations)
    : max_n_(max_n), max_states_(max_states), max_rotations_(max_rotations) {
  CHECK_CUDA(
      cudaMalloc(&d_grids_, max_states * max_n * max_n * sizeof(uint16_t)));
  CHECK_CUDA(cudaMalloc(&d_rotations_, max_rotations * sizeof(Rotation)));
  CHECK_CUDA(cudaMalloc(&d_zobrist_, 64 * 64 * 2048 * sizeof(uint64_t)));
  CHECK_CUDA(
      cudaMalloc(&d_results_, max_states * max_rotations * sizeof(GPUResult)));
}

CudaBeamSearch::~CudaBeamSearch() {
  cudaFree(d_grids_);
  cudaFree(d_rotations_);
  cudaFree(d_zobrist_);
  cudaFree(d_results_);
}

void CudaBeamSearch::set_zobrist_table(const uint64_t *flat_zobrist_table) {
  CHECK_CUDA(cudaMemcpy(d_zobrist_, flat_zobrist_table,
                        64 * 64 * 2048 * sizeof(uint64_t),
                        cudaMemcpyHostToDevice));
}

std::vector<GPUResult> CudaBeamSearch::evaluate_batch(
    const std::vector<uint16_t> &grids_flat, int num_states,
    const std::vector<std::tuple<int, int, int>> &rotations, int grid_size) {
  if (num_states == 0 || rotations.empty())
    return {};

  int num_rotations = rotations.size();

  // Check bounds
  if (num_states > max_states_ || num_rotations > max_rotations_) {
    std::cerr << "Batch bounds exceeded! states: " << num_states << "/"
              << max_states_ << ", rots: " << num_rotations << "/"
              << max_rotations_ << std::endl;
    return {};
  }

  std::vector<Rotation> rots(num_rotations);
  for (int i = 0; i < num_rotations; ++i) {
    rots[i].k = std::get<0>(rotations[i]);
    rots[i].i = std::get<1>(rotations[i]);
    rots[i].j = std::get<2>(rotations[i]);
  }

  CHECK_CUDA(cudaMemcpy(d_grids_, grids_flat.data(),
                        grids_flat.size() * sizeof(uint16_t),
                        cudaMemcpyHostToDevice));
  CHECK_CUDA(cudaMemcpy(d_rotations_, rots.data(),
                        num_rotations * sizeof(Rotation),
                        cudaMemcpyHostToDevice));

  int total_threads = num_states * num_rotations;
  int threads_per_block = 256;
  int blocks = (total_threads + threads_per_block - 1) / threads_per_block;

  evaluate_rotations_kernel<<<blocks, threads_per_block>>>(
      d_grids_, (Rotation *)d_rotations_, d_zobrist_, d_results_, grid_size,
      num_states, num_rotations);

  CHECK_CUDA(cudaGetLastError());
  CHECK_CUDA(cudaDeviceSynchronize());

  std::vector<GPUResult> results(total_threads);
  CHECK_CUDA(cudaMemcpy(results.data(), d_results_,
                        total_threads * sizeof(GPUResult),
                        cudaMemcpyDeviceToHost));

  return results;
}
