#pragma once
#include "gpu_common.h"
#include <cstdint>
#include <tuple>
#include <vector>

// ── Result type for crop beam scoring ────────────────────────────────────────
struct CropEvalResult {
    int parent_idx;   // which input state this came from
    int rot_idx;      // which rotation was applied (index into valid_rots)
    int score;        // heuristic score after rotation
};

// ── Max crop dimension supported on GPU ──────────────────────────────────────
// Matches ZOBRIST_MAX_N (64). Each thread needs ~18 KB stack; see constructor.
static constexpr int CCB_MAX_N   = 64;
static constexpr int CCB_MAX_VAL = 2048;   // CCB_MAX_N*CCB_MAX_N/2

class CudaBeamSearch {
public:
  CudaBeamSearch(int max_n, int max_states, int max_rotations);
  ~CudaBeamSearch();

  // ── Original beam-search API ──────────────────────────────────────────────
  void set_zobrist_table(const uint64_t *flat_zobrist_table);

  std::vector<GPUResult>
  evaluate_batch(const std::vector<uint16_t> &grids_flat, int num_states,
                 const std::vector<std::tuple<int, int, int>> &rotations,
                 int grid_size);

  // ── Crop beam API (pre_step / do_step) ────────────────────────────────────
  // Call once whenever `locked` changes (i.e. after each STEP_Do frame).
  // full_n  = current value of `n` (stride of the locked array).
  void update_locked(const std::vector<std::vector<uint8_t>> &locked, int full_n);

  // score_mode 0 → weighted_free_pairs  (pre_step_beam_search)
  // score_mode 1 → weighted_beam_DO     (Do_step_beam_search)
  //
  // valid_rots: (k, local_row, local_col) already filtered by Check_Valid.
  // cnt_g:  global `cnt` offset.
  std::vector<CropEvalResult>
  evaluate_crop(const std::vector<uint16_t> &states_flat, int num_states, int N,
                const std::vector<std::tuple<int, int, int>> &valid_rots,
                int cnt_g, int Fsize, int PD, int score_mode);

private:
  // ── Original buffers ──────────────────────────────────────────────────────
  int max_n_, max_states_, max_rotations_;
  uint16_t  *d_grids_     = nullptr;
  int       *d_rotations_ = nullptr;
  uint64_t  *d_zobrist_   = nullptr;
  GPUResult *d_results_   = nullptr;

  // ── Crop buffers (grown on first use / when capacity exceeded) ────────────
  uint16_t      *d_crop_states_  = nullptr;  size_t cap_crop_states_  = 0;
  int           *d_crop_rk_      = nullptr;
  int           *d_crop_rr_      = nullptr;
  int           *d_crop_rc_      = nullptr;  size_t cap_crop_rots_    = 0;
  uint8_t       *d_locked_       = nullptr;  size_t cap_locked_       = 0;
  CropEvalResult*d_crop_results_ = nullptr;  size_t cap_crop_results_ = 0;

  int locked_stride_ = 0;   // current full_n used as stride

  template<class T>
  static void ensure_buf(T *&ptr, size_t &cap, size_t need);
};
