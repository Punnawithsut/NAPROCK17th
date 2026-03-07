#include "cuda_beam_search.cuh"
#include <cmath>
#include <cuda_runtime.h>
#include <iostream>
#include <cstring>

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

// ═══════════════════════════════════════════════════════════════════════════
//  ORIGINAL KERNEL — unchanged
// ═══════════════════════════════════════════════════════════════════════════
__global__ void evaluate_rotations_kernel(
    const uint16_t *__restrict__ grids, const Rotation *__restrict__ rotations,
    const uint64_t *__restrict__ zobrist_table, GPUResult *__restrict__ results,
    int grid_size, int num_states, int num_rotations)
{
  int id = blockIdx.x * blockDim.x + threadIdx.x;
  if (id >= num_states * num_rotations)
    return;

  int state_idx = id / num_rotations;
  int rot_idx   = id % num_rotations;

  const uint16_t *original_grid = grids + (state_idx * grid_size * grid_size);
  Rotation rot = rotations[rot_idx];

  uint16_t local_grid[4096];
  for (int r = 0; r < grid_size; r++)
    for (int c = 0; c < grid_size; c++)
      local_grid[r * grid_size + c] = original_grid[r * grid_size + c];

  int k = rot.k, i = rot.i, j = rot.j;
  uint16_t temp[4096];
  for (int x = 0; x < k; x++)
    for (int y = 0; y < k; y++)
      temp[y * k + (k - 1 - x)] = local_grid[(i + x) * grid_size + (j + y)];
  for (int x = 0; x < k; x++)
    for (int y = 0; y < k; y++)
      local_grid[(i + x) * grid_size + (j + y)] = temp[x * k + y];

  int max_val = 0;
  uint64_t hash = 0;
  for (int r = 0; r < grid_size; r++) {
    for (int c = 0; c < grid_size; c++) {
      int val = local_grid[r * grid_size + c];
      if (val > max_val) max_val = val;
      long long z_idx = (r * 64 * 2048) + (c * 2048) + val;
      hash ^= zobrist_table[z_idx];
    }
  }

  struct Coord { int r, c; };
  Coord coords1[2048], coords2[2048];
  int coord_counts[2048];
  for (int v = 0; v <= max_val; ++v) coord_counts[v] = 0;

  for (int r = 0; r < grid_size; r++) {
    for (int c = 0; c < grid_size; c++) {
      int val = local_grid[r * grid_size + c];
      if (coord_counts[val] == 0) {
        coords1[val] = {r, c}; coord_counts[val] = 1;
      } else {
        coords2[val] = {r, c}; coord_counts[val] = 2;
      }
    }
  }

  int paired_count = 0, total_dist = 0;
  for (int v = 0; v <= max_val; ++v) {
    if (coord_counts[v] == 2) {
      int dr = abs(coords1[v].r - coords2[v].r);
      int dc = abs(coords1[v].c - coords2[v].c);
      int dist = dr + dc;
      if (dist == 1) paired_count++;
      total_dist += dist + max(dr, dc);
    }
  }

  results[id].paired_count  = paired_count;
  results[id].heuristic     = total_dist;
  results[id].hash          = hash;
  results[id].parent_idx    = state_idx;
  results[id].rotation_idx  = rot_idx;
}

// ═══════════════════════════════════════════════════════════════════════════
//  DEVICE HELPERS for crop scoring
// ═══════════════════════════════════════════════════════════════════════════

// In-place 90° CW rotation of k×k block at (sr,sc) in an N-wide row-major grid.
__device__ __forceinline__
void d_rot_crop(uint16_t *g, int N, int k, int sr, int sc)
{
    uint16_t tmp[CCB_MAX_N * CCB_MAX_N];
    for (int x = 0; x < k; ++x)
        for (int y = 0; y < k; ++y)
            tmp[y * k + (k - 1 - x)] = g[(sr + x) * N + (sc + y)];
    for (int x = 0; x < k; ++x)
        for (int y = 0; y < k; ++y)
            g[(sr + x) * N + (sc + y)] = tmp[x * k + y];
}

// Mirrors CPU: weighted_free_pairs(crop, Fsize, local_cnt)
__device__
int d_score_free_pairs(const uint16_t *g, int N, int Fsize, int local_cnt,
                        const uint8_t *lk, int lk_stride, int cnt_g)
{
    const int half = N / 2;
    const int mrs  = half - 2;
    const int mre  = half - 1;
    int score = 0;

    // Close-pair bonus
    bool    seen  [CCB_MAX_VAL] = {};
    int16_t seen_r[CCB_MAX_VAL];
    int16_t seen_c[CCB_MAX_VAL];

    const int jlim = min(N - 1, local_cnt + Fsize / 4);
    for (int i = mrs; i < N - 1; ++i) {
        for (int j = local_cnt; j < jlim; ++j) {
            if (lk[(cnt_g + i) * lk_stride + (cnt_g + j)]) continue;
            uint16_t v = g[i * N + j];
            if (v >= CCB_MAX_VAL) continue;
            if (seen[v]) {
                if (abs((int)seen_r[v] - i) <= 1 || abs((int)seen_c[v] - j) <= 1)
                    score += 2;
            } else {
                seen[v]   = true;
                seen_r[v] = (int16_t)i;
                seen_c[v] = (int16_t)j;
            }
        }
    }

    // Horizontal pair weights (first match per column scan)
    for (int j = local_cnt; j < N; ++j) {
        for (int i = mrs; i < N; ++i) {
            if (lk[(cnt_g + i) * lk_stride + (cnt_g + j)]) continue;
            if (j + 1 < N &&
                !lk[(cnt_g + i) * lk_stride + (cnt_g + j + 1)] &&
                g[i * N + j] == g[i * N + j + 1])
            {
                bool is_lower  = (i >= mre);
                bool is_upper  = (i <= mrs);
                bool is_center = (j >= half - 2 + local_cnt && j <= half + 1 + local_cnt);
                bool is_right  = (j >= half + local_cnt);
                int w;
                if      (i == mre)  w = 7;
                else if (is_lower)  w = is_center ? 8 : (is_right ? 4 : 8);
                else if (is_upper)  w = 1;
                else                w = 1;
                score += w;
                break;
            }
        }
    }

    // Vertical pair weights
    for (int i = mrs - 1; i < N; ++i) {
        for (int j = local_cnt; j < N; ++j) {
            if (lk[(cnt_g + i) * lk_stride + (cnt_g + j)]) continue;
            if (i + 1 < N &&
                !lk[(cnt_g + i + 1) * lk_stride + (cnt_g + j)] &&
                g[i * N + j] == g[(i + 1) * N + j])
            {
                bool is_lower  = (i >= mre);
                bool is_upper  = (i <= mrs);
                bool is_center = (j >= half - 2 + local_cnt && j <= half + 1 + local_cnt);
                bool is_right  = (j >= half + local_cnt);
                int w;
                if      (i == mrs)  w = 5;
                else if (is_lower)  w = is_center ? 3 : (is_right ? 1 : 3);
                else if (is_upper)  w = (mrs - 1 == i) ? 3 : 1;
                else                w = 1;
                score += w;
            }
        }
    }
    return score;
}

// Mirrors CPU: weighted_beam_DO(crop, Fsize, PD)
__device__
int d_score_beam_do(const uint16_t *g, int N, int Fsize, int PD,
                     const uint8_t *lk, int lk_stride, int cnt_g)
{
    const int half = N / 2;
    const int mrs  = half - 2;
    const int mre  = half - 1;
    int score = 0;

    // Prefix aligned pairs
    int CUR_J = 0;
    for (int j = PD; j < half; )
    {
        if (g[mrs * N + j] == g[mre * N + j]) {
            ++j; score += 1000;
        } else if (j != half - 1 &&
                   g[mrs * N + j]     == g[mrs * N + j + 1] &&
                   g[mre * N + j]     == g[mre * N + j + 1]) {
            j += 2; score += 2000;
        } else break;
        CUR_J = j;
    }
    if (CUR_J >= half) return 99999;

    // Close-pair bonus
    bool    seen  [CCB_MAX_VAL] = {};
    int16_t seen_r[CCB_MAX_VAL];
    int16_t seen_c[CCB_MAX_VAL];

    const int jlim = min(N - 1, CUR_J + Fsize / 4);
    for (int i = mrs; i < N - 1; ++i) {
        for (int j = CUR_J; j < jlim; ++j) {
            if (lk[(cnt_g + i) * lk_stride + (cnt_g + j)]) continue;
            uint16_t v = g[i * N + j];
            if (v >= CCB_MAX_VAL) continue;
            if (seen[v]) {
                if (abs((int)seen_r[v] - i) <= 1 || abs((int)seen_c[v] - j) <= 1)
                    score += 2;
            } else {
                seen[v]   = true;
                seen_r[v] = (int16_t)i;
                seen_c[v] = (int16_t)j;
            }
        }
    }

    // Horizontal pair weights (first per column)
    for (int j = CUR_J; j < N; ++j) {
        for (int i = mrs; i < N; ++i) {
            if (lk[(cnt_g + i) * lk_stride + (cnt_g + j)]) continue;
            if (j + 1 < N &&
                !lk[(cnt_g + i) * lk_stride + (cnt_g + j + 1)] &&
                g[i * N + j] == g[i * N + j + 1])
            {
                bool is_lower  = (i >= mre);
                bool is_upper  = (i <= mrs);
                bool is_center = (j >= half - 2 && j <= half + 1);
                bool is_right  = (j >= half);
                int w;
                if      (i == mre)  w = 7;
                else if (is_lower)  w = is_center ? 8 : (is_right ? 4 : 8);
                else if (is_upper)  w = 1;
                else                w = 1;
                score += w;
                break;
            }
        }
    }

    // Vertical pair weights
    for (int i = mrs - 1; i < N; ++i) {
        for (int j = CUR_J; j < N; ++j) {
            if (lk[(cnt_g + i) * lk_stride + (cnt_g + j)]) continue;
            if (i + 1 < N &&
                !lk[(cnt_g + i + 1) * lk_stride + (cnt_g + j)] &&
                g[i * N + j] == g[(i + 1) * N + j])
            {
                bool is_lower  = (i >= mre);
                bool is_upper  = (i <= mrs);
                bool is_center = (j >= half - 2 && j <= half + 1);
                bool is_right  = (j >= half);
                int w;
                if      (i == mrs)  w = 5;
                else if (is_lower)  w = is_center ? 3 : (is_right ? 1 : 3);
                else if (is_upper)  w = (mrs - 1 == i) ? 3 : 1;
                else                w = 1;
                score += w;
            }
        }
    }
    return score;
}

// ═══════════════════════════════════════════════════════════════════════════
//  CROP KERNEL  —  one thread per (state × rotation)
//  score_mode 0 → free_pairs,  1 → beam_do
// ═══════════════════════════════════════════════════════════════════════════
__global__ void evaluate_crop_kernel(
    const uint16_t *__restrict__ states,   // [S × N × N]
    int S, int N,
    const int *__restrict__ rk,            // [R]
    const int *__restrict__ rr,
    const int *__restrict__ rc,
    int R,
    const uint8_t *__restrict__ lk,        // [lk_stride × lk_stride]
    int lk_stride, int cnt_g,
    int Fsize, int PD,
    int score_mode,
    CropEvalResult *__restrict__ out       // [S × R]
)
{
    const int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= S * R) return;

    const int si = tid / R;
    const int ri = tid % R;

    // Copy state into thread-local grid
    uint16_t g[CCB_MAX_N * CCB_MAX_N];
    const uint16_t *src = states + si * N * N;
    for (int i = 0; i < N * N; ++i) g[i] = src[i];

    // Apply rotation
    d_rot_crop(g, N, rk[ri], rr[ri], rc[ri]);

    // Score
    int score = (score_mode == 0)
        ? d_score_free_pairs(g, N, Fsize, PD, lk, lk_stride, cnt_g)
        : d_score_beam_do   (g, N, Fsize, PD, lk, lk_stride, cnt_g);

    out[si * R + ri] = { si, ri, score };
}

// ═══════════════════════════════════════════════════════════════════════════
//  HOST  — CudaBeamSearch implementation
// ═══════════════════════════════════════════════════════════════════════════

template<class T>
void CudaBeamSearch::ensure_buf(T *&ptr, size_t &cap, size_t need)
{
    if (need > cap) {
        if (ptr) CHECK_CUDA(cudaFree(ptr));
        CHECK_CUDA(cudaMalloc(&ptr, need));
        cap = need;
    }
}

CudaBeamSearch::CudaBeamSearch(int max_n, int max_states, int max_rotations)
    : max_n_(max_n), max_states_(max_states), max_rotations_(max_rotations)
{
    CHECK_CUDA(cudaMalloc(&d_grids_,    max_states * max_n * max_n * sizeof(uint16_t)));
    CHECK_CUDA(cudaMalloc(&d_rotations_,max_rotations * sizeof(Rotation)));
    CHECK_CUDA(cudaMalloc(&d_zobrist_,  64 * 64 * 2048 * sizeof(uint64_t)));
    CHECK_CUDA(cudaMalloc(&d_results_,  max_states * max_rotations * sizeof(GPUResult)));

    // Each crop kernel thread needs ~18 KB stack:
    //   8 KB grid copy  +  3×2 KB seen arrays  +  tmp rotation buffer
    CHECK_CUDA(cudaDeviceSetLimit(cudaLimitStackSize, 48 * 1024));
}

CudaBeamSearch::~CudaBeamSearch()
{
    cudaFree(d_grids_);
    cudaFree(d_rotations_);
    cudaFree(d_zobrist_);
    cudaFree(d_results_);

    if (d_crop_states_)  cudaFree(d_crop_states_);
    if (d_crop_rk_)      { cudaFree(d_crop_rk_); cudaFree(d_crop_rr_); cudaFree(d_crop_rc_); }
    if (d_locked_)       cudaFree(d_locked_);
    if (d_crop_results_) cudaFree(d_crop_results_);
}

void CudaBeamSearch::set_zobrist_table(const uint64_t *flat)
{
    CHECK_CUDA(cudaMemcpy(d_zobrist_, flat,
                          64 * 64 * 2048 * sizeof(uint64_t),
                          cudaMemcpyHostToDevice));
}

std::vector<GPUResult>
CudaBeamSearch::evaluate_batch(const std::vector<uint16_t> &grids_flat,
                                int num_states,
                                const std::vector<std::tuple<int,int,int>> &rotations,
                                int grid_size)
{
    if (num_states == 0 || rotations.empty()) return {};

    int num_rotations = (int)rotations.size();
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

    int total = num_states * num_rotations;
    int tpb   = 256;
    evaluate_rotations_kernel<<<(total + tpb - 1) / tpb, tpb>>>(
        d_grids_, (Rotation *)d_rotations_, d_zobrist_, d_results_,
        grid_size, num_states, num_rotations);

    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    std::vector<GPUResult> results(total);
    CHECK_CUDA(cudaMemcpy(results.data(), d_results_,
                          total * sizeof(GPUResult),
                          cudaMemcpyDeviceToHost));
    return results;
}

// ── update_locked ─────────────────────────────────────────────────────────────
void CudaBeamSearch::update_locked(const std::vector<std::vector<uint8_t>> &locked,
                                    int full_n)
{
    locked_stride_ = full_n;
    size_t bytes   = (size_t)full_n * full_n * sizeof(uint8_t);
    ensure_buf(d_locked_, cap_locked_, bytes);

    // Flatten row-major
    std::vector<uint8_t> flat(full_n * full_n);
    for (int i = 0; i < full_n; ++i)
        for (int j = 0; j < full_n; ++j)
            flat[i * full_n + j] = locked[i][j];

    CHECK_CUDA(cudaMemcpy(d_locked_, flat.data(), bytes, cudaMemcpyHostToDevice));
}

// ── evaluate_crop ─────────────────────────────────────────────────────────────
std::vector<CropEvalResult>
CudaBeamSearch::evaluate_crop(const std::vector<uint16_t> &states_flat,
                               int num_states, int N,
                               const std::vector<std::tuple<int,int,int>> &valid_rots,
                               int cnt_g, int Fsize, int PD, int score_mode)
{
    if (num_states == 0 || valid_rots.empty()) return {};

    if (N > CCB_MAX_N) {
        std::cerr << "[CropEval] N=" << N << " exceeds CCB_MAX_N=" << CCB_MAX_N
                  << "; use CPU fallback.\n";
        return {};
    }

    const int R     = (int)valid_rots.size();
    const int total = num_states * R;

    // Grow device buffers as needed
    ensure_buf(d_crop_states_,  cap_crop_states_,  states_flat.size() * sizeof(uint16_t));
    {
        size_t rb = R * sizeof(int);
        if (rb > cap_crop_rots_) {
            if (d_crop_rk_) { cudaFree(d_crop_rk_); cudaFree(d_crop_rr_); cudaFree(d_crop_rc_); }
            CHECK_CUDA(cudaMalloc(&d_crop_rk_, rb));
            CHECK_CUDA(cudaMalloc(&d_crop_rr_, rb));
            CHECK_CUDA(cudaMalloc(&d_crop_rc_, rb));
            cap_crop_rots_ = rb;
        }
    }
    ensure_buf(d_crop_results_, cap_crop_results_, total * sizeof(CropEvalResult));

    // Unpack SoA
    std::vector<int> rk(R), rr(R), rc(R);
    for (int i = 0; i < R; ++i) {
        rk[i] = std::get<0>(valid_rots[i]);
        rr[i] = std::get<1>(valid_rots[i]);
        rc[i] = std::get<2>(valid_rots[i]);
    }

    CHECK_CUDA(cudaMemcpy(d_crop_states_, states_flat.data(),
                          states_flat.size() * sizeof(uint16_t),
                          cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_crop_rk_, rk.data(), R * sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_crop_rr_, rr.data(), R * sizeof(int), cudaMemcpyHostToDevice));
    CHECK_CUDA(cudaMemcpy(d_crop_rc_, rc.data(), R * sizeof(int), cudaMemcpyHostToDevice));

    const int tpb    = 128;
    const int blocks = (total + tpb - 1) / tpb;
    evaluate_crop_kernel<<<blocks, tpb>>>(
        d_crop_states_, num_states, N,
        d_crop_rk_, d_crop_rr_, d_crop_rc_, R,
        d_locked_, locked_stride_, cnt_g,
        Fsize, PD, score_mode,
        d_crop_results_);

    CHECK_CUDA(cudaGetLastError());
    CHECK_CUDA(cudaDeviceSynchronize());

    std::vector<CropEvalResult> results(total);
    CHECK_CUDA(cudaMemcpy(results.data(), d_crop_results_,
                          total * sizeof(CropEvalResult),
                          cudaMemcpyDeviceToHost));
    return results;
}
