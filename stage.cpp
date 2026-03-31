#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <queue>
#include <random>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

using namespace std;
using namespace std::chrono;

int n;
int size_grid = 12;
// for 10% buffer incase theoratical limit does not find a solution
int buffer = (size_grid * size_grid) * 0.55;
auto start_time = high_resolution_clock::now();
const double TIME_LIMIT = 260.0; // Slightly lower to be safe
uint64_t zobrist[144][1600];

void init_zobrist() {
  mt19937_64 rng(1337);
  for (int i = 0; i < 144; i++) {
    for (int j = 0; j < 1600; j++) {
      zobrist[i][j] = rng();
    }
  }
}

uint64_t compute_hash(const vector<int> &grid) {
  uint64_t h = 0;
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      int val = grid[i * n + j];
      h ^= zobrist[(i * n + j) % 144][val];
    }
  }
  return h;
}

struct Point {
  int r, c;
  bool operator==(const Point &other) const {
    return r == other.r && c == other.c;
  }
};
Point pos_cache[2048][2];

struct Rotation {
  int k, i, j;
  Rotation(int k, int i, int j) : k(k), i(i), j(j) {}
};

struct GridState {
  vector<int> grid;
  vector<Rotation> path;
  int real_score;
  double unified_score;
  uint64_t hash;
  vector<pair<Point, Point>> val_positions;

  bool operator<(const GridState &other) const {
    return unified_score < other.unified_score;
  }
};

vector<int> gen_rand_grid(int n) {
  srand(time(0));
  int num_values = n * n / 2;
  vector<int> values(num_values, 2);
  vector<int> grid(n * n);

  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      bool assigned = false;
      while (!assigned) {
        int idx = rand() % num_values;
        if (values[idx] > 0) {
          grid[i * n + j] = idx + 1;
          values[idx]--;
          assigned = true;
        }
      }
    }
  }
  return grid;
}

bool is_time_up() {
  auto now = high_resolution_clock::now();
  return duration<double>(now - start_time).count() >= TIME_LIMIT;
}

int count_adjacent_pairs(const vector<int> &grid) {
  int count = 0;
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      int val = grid[i * n + j];
      if (i + 1 < n && val == grid[(i + 1) * n + j])
        count++;
      if (j + 1 < n && val == grid[i * n + (j + 1)])
        count++;
    }
  }
  return count;
}

// Check how many pairs are fully contained and adjacent within a specific row
// range [start_row, end_row)
int count_pairs_in_range(const vector<int> &grid,
                         const vector<pair<Point, Point>> &positions,
                         int start_row, int end_row) {
  int count = 0;
  int num_values = n * n / 2;
  for (int v = 1; v <= num_values; v++) {
    const auto &p1 = positions[v].first;
    const auto &p2 = positions[v].second;

    // Check if both points are in range
    bool p1_in = (p1.r >= start_row && p1.r < end_row);
    bool p2_in = (p2.r >= start_row && p2.r < end_row);

    if (p1_in && p2_in) {
      int dist = abs(p1.r - p2.r) + abs(p1.c - p2.c);
      if (dist == 1)
        count++;
    }
  }
  return count;
}

// --- Staged Scoring Logic ---

// Calculate score for a single pair given the active stage text
// target_start_row: The start of the rows we are currently trying to fill
// (e.g., 0, 2, 4...) Score a single pair relative to the active target stage
double get_pair_score_staged(const Point &p1, const Point &p2,
                             int target_start_row, int target_num_rows) {
  int target_end_row = target_start_row + target_num_rows;

  bool p1_in_target = (p1.r >= target_start_row && p1.r < target_end_row);
  bool p2_in_target = (p2.r >= target_start_row && p2.r < target_end_row);

  bool p1_below = (p1.r >= target_end_row);
  bool p2_below = (p2.r >= target_end_row);

  // Note: Items above target_start_row are "locked" and shouldn't change,
  int dist = abs(p1.r - p2.r) + abs(p1.c - p2.c);

  // PRIORITY 1: Form pairs inside the target region
  if (p1_in_target && p2_in_target) {
    if (dist == 1)
      return 10000.0;           // Solved!
    return 500.0 - dist * 10.0; // In target, needs adjacency
  }

  // PRIORITY 2: Bring missing partners into the region
  if ((p1_in_target && p2_below) || (p2_in_target && p1_below)) {
    return 100.0 - dist * 5.0;
  }

  // PRIORITY 3: Keep potential future pairs somewhat close (tie-breaker)
  if (p1_below && p2_below) {
    if (dist == 1)
      return 10.0;
    return -dist * 0.5;
  }

  return 0.0;
}

pair<double, vector<pair<Point, Point>>>
calculate_initial_staged_score(const vector<int> &grid, int target_start_row,
                               int target_num_rows) {
  int num_values = n * n / 2;
  vector<pair<Point, Point>> positions(num_values + 1);
  static int found_count[2048];
  for (int v = 1; v <= num_values; ++v)
    found_count[v] = 0;

  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      int val = grid[i * n + j];
      int idx = found_count[val]++;
      if (idx == 0)
        positions[val].first = {i, j};
      else
        positions[val].second = {i, j};
    }
  }

  double score = 0;
  for (int v = 1; v <= num_values; v++) {
    score += get_pair_score_staged(positions[v].first, positions[v].second,
                                   target_start_row, target_num_rows);
  }
  return {score, positions};
}

pair<double, vector<pair<Point, Point>>> calculate_incremental_staged_update(
    double old_score, const vector<pair<Point, Point>> &old_pos, int k, int i,
    int j, const vector<int> &grid, int target_start_row, int target_num_rows) {
  vector<pair<Point, Point>> new_pos = old_pos;
  double new_score = old_score;
  static bool affected_marker[2048];

  int num_values = n * n / 2;
  for (int v = 1; v <= num_values; ++v)
    affected_marker[v] = false;

  vector<int> affected_values;
  for (int x = 0; x < k; ++x) {
    for (int y = 0; y < k; ++y) {
      int val = grid[(i + x) * n + (j + y)];
      if (!affected_marker[val]) {
        affected_marker[val] = true;
        affected_values.push_back(val);
      }
    }
  }

  for (int val : affected_values) {
    new_score -= get_pair_score_staged(new_pos[val].first, new_pos[val].second,
                                       target_start_row, target_num_rows);
  }

  for (int val : affected_values) {
    Point &p1 = new_pos[val].first;
    if (p1.r >= i && p1.r < i + k && p1.c >= j && p1.c < j + k) {
      int r_rel = p1.r - i;
      int c_rel = p1.c - j;
      p1.r = i + c_rel;
      p1.c = j + k - 1 - r_rel;
    }
    Point &p2 = new_pos[val].second;
    if (p2.r >= i && p2.r < i + k && p2.c >= j && p2.c < j + k) {
      int r_rel = p2.r - i;
      int c_rel = p2.c - j;
      p2.r = i + c_rel;
      p2.c = j + k - 1 - r_rel;
    }
  }

  for (int val : affected_values) {
    new_score += get_pair_score_staged(new_pos[val].first, new_pos[val].second,
                                       target_start_row, target_num_rows);
  }

  return {new_score, new_pos};
}

// ----------------

vector<int> rotate_submatrix(const vector<int> &grid, int k, int i, int j) {
  vector<int> new_grid = grid;
  for (int x = 0; x < k; x++) {
    for (int y = 0; y < k; y++) {
      new_grid[(i + y) * n + (j + k - 1 - x)] = grid[(i + x) * n + (j + y)];
    }
  }
  return new_grid;
}

uint64_t compute_hash_incremental(uint64_t old_hash,
                                  const vector<int> &old_grid,
                                  const vector<int> &new_grid, int k, int i,
                                  int j) {
  uint64_t h = old_hash;
  for (int x = 0; x < k; ++x) {
    for (int y = 0; y < k; ++y) {
      int r = i + x;
      int c = j + y;
      h ^= zobrist[(r * n + c) % 144][old_grid[r * n + c]];
      h ^= zobrist[(r * n + c) % 144][new_grid[r * n + c]];
    }
  }
  return h;
}

struct ChildCand {
  vector<int> grid;
  int k, i, j;
  double unified_score;
  uint64_t hash;
};

// Beam search for a specific stage
// Returns the path to complete this stage
vector<Rotation> perform_staged_beam_run(const vector<int> &initial_grid,
                                         int target_start_row,
                                         int target_num_rows,
                                         int max_depth_limit) {
  int cells_in_region = target_num_rows * n;
  int target_pairs = cells_in_region / 2;

  int beam_width = 4000;
  int children_per_state = 100;

  if (target_num_rows > 2) {
    beam_width = 5000; // Increased for larger stages
  }

  priority_queue<GridState> beam;
  auto init_data = calculate_initial_staged_score(
      initial_grid, target_start_row, target_num_rows);
  double init_u = init_data.first;
  uint64_t init_hash = compute_hash(initial_grid);

  beam.push({initial_grid, {}, 0, init_u, init_hash, init_data.second});

  GridState best_state = beam.top();
  int best_stage_progress =
      count_pairs_in_range(initial_grid, init_data.second, target_start_row,
                           target_start_row + target_num_rows);

  if (best_stage_progress == target_pairs)
    return {};

  unordered_set<uint64_t> visited;
  visited.insert(init_hash);

  for (int depth = 0; depth < max_depth_limit; ++depth) {
    if (is_time_up())
      break;

    priority_queue<GridState> next_beam;
    vector<GridState> current_beam_vec;
    while (!beam.empty()) {
      current_beam_vec.push_back(beam.top());
      beam.pop();
    }

    for (const auto &current : current_beam_vec) {
      int progress = count_pairs_in_range(current.grid, current.val_positions,
                                          target_start_row,
                                          target_start_row + target_num_rows);

      // Early exit if solved
      if (progress == target_pairs)
        return current.path;

      if (progress > best_stage_progress) {
        best_stage_progress = progress;
        best_state = current;
      }

      vector<ChildCand> candidates;
      int max_k = min(n, 7);

      for (int k = 2; k <= max_k; ++k) {
        for (int i = target_start_row; i <= n - k; ++i) {
          for (int j = 0; j <= n - k; ++j) {
            vector<int> new_grid = rotate_submatrix(current.grid, k, i, j);
            uint64_t new_hash = compute_hash_incremental(
                current.hash, current.grid, new_grid, k, i, j);

            if (visited.count(new_hash))
              continue;

            auto update = calculate_incremental_staged_update(
                current.unified_score, current.val_positions, k, i, j,
                current.grid, target_start_row, target_num_rows);

            // Length Penalty: Encourages shorter paths
            double score_with_penalty =
                update.first - (current.path.size() + 1) * 2.0;

            candidates.push_back(
                {new_grid, k, i, j, score_with_penalty, new_hash});
          }
        }
      }

      if (candidates.size() > children_per_state) {
        partial_sort(candidates.begin(),
                     candidates.begin() + children_per_state, candidates.end(),
                     [](const ChildCand &a, const ChildCand &b) {
                       return a.unified_score > b.unified_score;
                     });
        candidates.resize(children_per_state);
      } else {
        sort(candidates.begin(), candidates.end(),
             [](const ChildCand &a, const ChildCand &b) {
               return a.unified_score > b.unified_score;
             });
      }

      for (const auto &cand : candidates) {
        visited.insert(cand.hash);
        vector<Rotation> new_path = current.path;
        new_path.emplace_back(cand.k, cand.i, cand.j);

        // Recompute accurate score for next state
        auto update = calculate_incremental_staged_update(
            current.unified_score, current.val_positions, cand.k, cand.i,
            cand.j, current.grid, target_start_row, target_num_rows);

        next_beam.push(
            {cand.grid, new_path, 0, update.first, cand.hash, update.second});
      }
    }

    int kept = 0;
    while (!next_beam.empty() && kept < beam_width) {
      beam.push(next_beam.top());
      next_beam.pop();
      kept++;
    }
    if (beam.empty())
      break;
  }
  return best_state.path;
}

vector<int> apply_rotations(vector<int> grid, const vector<Rotation> &path) {
  for (const auto &rot : path) {
    grid = rotate_submatrix(grid, rot.k, rot.i, rot.j);
  }
  return grid;
}

void print_grid(const vector<int> &grid) {
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      cout << setw(2) << grid[i * n + j] << " ";
    }
    cout << "\n";
  }
}

int main() {
  init_zobrist();

  // Use fixed seed or user gen
  vector<int> grid = gen_rand_grid(size_grid);
  n = size_grid;

  cout << "Initial grid:\n";
  print_grid(grid);
  cout << "\nInitial pairs: " << count_adjacent_pairs(grid) << "/"
       << (n * n / 2) << "\n\n";

  start_time = high_resolution_clock::now();

  vector<Rotation> full_solution;
  vector<int> current_grid = grid;

  // Strategy: Mixed Staging (2-row stages then 4-row finish)
  // This balances precision for early rows with optimization for the final
  // block
  for (int row = 0; row < n;) {
    if (is_time_up())
      break;

    int rows_to_solve = 2;
    // For the last 4 rows (8, 9, 10, 11), solve them together as one block
    // This helps coordinate the final pieces better than doing 8-9 then 10-11
    if (row == 8) {
      rows_to_solve = 4;
    }

    cout << ">>> Solving Stage: Rows " << row << "-" << row + rows_to_solve - 1
         << " <<<\n";
    int max_depth = 50;

    vector<Rotation> stage_path =
        perform_staged_beam_run(current_grid, row, rows_to_solve, max_depth);

    for (auto &rot : stage_path) {
      full_solution.push_back(rot);
      current_grid = rotate_submatrix(current_grid, rot.k, rot.i, rot.j);
    }

    auto init_data =
        calculate_initial_staged_score(current_grid, row, rows_to_solve);
    int pairs = count_pairs_in_range(current_grid, init_data.second, row,
                                     row + rows_to_solve);
    int target = (rows_to_solve * n) / 2;
    cout << "Stage Result: " << pairs << "/" << target << " pairs. Added "
         << stage_path.size() << " moves.\n";

    row += rows_to_solve;
  }

  // Global Path Optimization
  vector<Rotation> optimized_path = full_solution;
  int final_score = count_adjacent_pairs(current_grid);
  {
    bool improved = true;
    vector<int> start_g = grid;
    while (improved) {
      improved = false;
      for (size_t i = 0; i < optimized_path.size(); ++i) {
        vector<Rotation> next_path = optimized_path;
        next_path.erase(next_path.begin() + i);
        vector<int> res = apply_rotations(start_g, next_path);
        if (count_adjacent_pairs(res) >= final_score) {
          optimized_path = next_path;
          improved = true;
          break;
        }
      }
    }
    full_solution = optimized_path;
  }

  auto elapsed =
      duration<double>(high_resolution_clock::now() - start_time).count();
  final_score = count_adjacent_pairs(apply_rotations(grid, full_solution));

  int limit = 100; // <= 100 moves constraint

  cout << "\n=== RESULTS ===\n";
  cout << "Moves: " << full_solution.size() << " / " << limit;
  cout << "Score: " << final_score << "/" << (n * n / 2) << "\n";

  bool solved = (final_score == n * n / 2);
  bool within_limit = (full_solution.size() <= limit);

  cout << "Time: " << fixed << setprecision(2) << elapsed << "s\n";

  return 0;
}
