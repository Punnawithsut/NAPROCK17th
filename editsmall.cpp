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
const double TIME_LIMIT = 270.0;
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

double calculate_unified_score(const vector<int> &grid) {
  int num_values = n * n / 2;
  static int found_count[2048];
  for (int v = 1; v <= num_values; ++v)
    found_count[v] = 0;

  for (int i = 0; i < n; i++) {
    for (int j = 0; j < n; j++) {
      int val = grid[i * n + j];
      int idx = found_count[val]++;
      pos_cache[val][idx] = {i, j};
    }
  }

  double score = 0;
  for (int v = 1; v <= num_values; v++) {
    const auto &p1 = pos_cache[v][0];
    const auto &p2 = pos_cache[v][1];

    int dist = abs(p1.r - p2.r) + abs(p1.c - p2.c);

    if (dist == 1)
      score += 100.0;
    else if (dist == 2)
      score += 20.0;
    else if (dist == 3)
      score += 5.0;
    else if (dist == 4)
      score += 1.0;
    else
      score -= dist * 0.5;
  }
  return score;
}

// Pair of (Unified Score, Val Positions Map)
pair<double, vector<pair<Point, Point>>>
calculate_initial_score_and_pos(const vector<int> &grid) {
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
    const auto &p1 = positions[v].first;
    const auto &p2 = positions[v].second;

    int dist = abs(p1.r - p2.r) + abs(p1.c - p2.c);

    if (dist == 1)
      score += 100.0;
    else if (dist == 2)
      score += 20.0;
    else if (dist == 3)
      score += 5.0;
    else if (dist == 4)
      score += 1.0;
    else
      score -= dist * 0.5;
  }

  return {score, positions};
}

pair<double, vector<pair<Point, Point>>>
calculate_incremental_update(double old_score,
                             const vector<pair<Point, Point>> &old_pos, int k,
                             int i, int j, const vector<int> &grid) {
  vector<pair<Point, Point>> new_pos = old_pos;
  double new_score = old_score;
  int num_values = n * n / 2;
  static bool affected_marker[2048]; // Max values
  for (int v = 1; v <= num_values; ++v)
    affected_marker[v] = false;

  vector<int> affected_values;
  // Identify affected values in the rotating region
  for (int x = 0; x < k; ++x) {
    for (int y = 0; y < k; ++y) {
      int val = grid[(i + x) * n + (j + y)];
      if (!affected_marker[val]) {
        affected_marker[val] = true;
        affected_values.push_back(val);
      }
    }
  }

  // Remove old contribution
  for (int val : affected_values) {
    const auto &p1 = new_pos[val].first;
    const auto &p2 = new_pos[val].second;
    int dist = abs(p1.r - p2.r) + abs(p1.c - p2.c);

    if (dist == 1)
      new_score -= 100.0;
    else if (dist == 2)
      new_score -= 20.0;
    else if (dist == 3)
      new_score -= 5.0;
    else if (dist == 4)
      new_score -= 1.0;
    else
      new_score -= (-dist * 0.5); // Subtracting negative = adding positive
  }

  // Update positions safely
  for (int val : affected_values) {
    // Check first point
    Point &p1 = new_pos[val].first;
    if (p1.r >= i && p1.r < i + k && p1.c >= j && p1.c < j + k) {
      int r_rel = p1.r - i;
      int c_rel = p1.c - j;
      p1.r = i + c_rel;
      p1.c = j + k - 1 - r_rel;
    }

    // Check second point
    Point &p2 = new_pos[val].second;
    if (p2.r >= i && p2.r < i + k && p2.c >= j && p2.c < j + k) {
      int r_rel = p2.r - i;
      int c_rel = p2.c - j;
      p2.r = i + c_rel;
      p2.c = j + k - 1 - r_rel;
    }
  }

  // Add new contribution
  for (int val : affected_values) {
    const auto &p1 = new_pos[val].first;
    const auto &p2 = new_pos[val].second;
    int dist = abs(p1.r - p2.r) + abs(p1.c - p2.c);

    if (dist == 1)
      new_score += 100.0;
    else if (dist == 2)
      new_score += 20.0;
    else if (dist == 3)
      new_score += 5.0;
    else if (dist == 4)
      new_score += 1.0;
    else
      new_score += (-dist * 0.5);
  }

  return {new_score, new_pos};
}

bool is_solved(const vector<int> &grid) {
  return count_adjacent_pairs(grid) == n * n / 2;
}

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
  int real_score;
  double unified_score;
  uint64_t hash;
};

// Forward Declaration
vector<int> apply_rotations(vector<int> grid, const vector<Rotation> &path);

vector<Rotation> perform_beam_run(const vector<int> &initial_grid,
                                  int width_override, int max_depth_override) {
  int target_score = n * n / 2;
  int max_depth = (max_depth_override > 0) ? max_depth_override : target_score;

  int beam_width = width_override;
  int children_per_state = 20;

  if (n <= 8) {
    beam_width = max(beam_width, 800);
  } else if (n <= 10) {
    beam_width = max(beam_width, 400);
  }

  cout << "=== Beam Run (Width=" << beam_width << ", Depth=" << max_depth
       << ") ===\n";

  priority_queue<GridState> beam;
  auto init_data = calculate_initial_score_and_pos(initial_grid);
  double init_u = init_data.first;
  uint64_t init_hash = compute_hash(initial_grid);

  beam.push({initial_grid,
             {},
             count_adjacent_pairs(initial_grid),
             init_u,
             init_hash,
             init_data.second});

  GridState best = {initial_grid, {},        count_adjacent_pairs(initial_grid),
                    init_u,       init_hash, init_data.second};

  unordered_set<uint64_t> global_visited;
  global_visited.insert(init_hash);

  for (int depth = 0; depth < max_depth && !beam.empty(); depth++) {
    if (is_time_up())
      break;

    priority_queue<GridState> next_beam;
    vector<GridState> current_beam_vec;
    while (!beam.empty()) {
      current_beam_vec.push_back(beam.top());
      beam.pop();
    }

    int best_score_this_level = -1;

    for (const auto &current : current_beam_vec) {
      if (current.real_score > best.real_score) {
        best = current;
      }
      if (current.real_score == target_score)
        return current.path;

      int max_k = min(n, 7);

      vector<ChildCand> candidates;

      for (int k = 2; k <= max_k; ++k) {
        for (int i = 0; i <= n - k; ++i) {
          for (int j = 0; j <= n - k; ++j) {
            vector<int> new_grid = rotate_submatrix(current.grid, k, i, j);
            uint64_t new_hash = compute_hash_incremental(
                current.hash, current.grid, new_grid, k, i, j);

            if (global_visited.count(new_hash))
              continue;

            int new_real = count_adjacent_pairs(new_grid);
            if (new_real == target_score) {
              vector<Rotation> path = current.path;
              path.emplace_back(k, i, j);
              return path;
            }

            auto update = calculate_incremental_update(current.unified_score,
                                                       current.val_positions, k,
                                                       i, j, current.grid);
            double new_u = update.first;
            // Deterministic: No randomness here

            candidates.push_back(
                {new_grid, k, i, j, new_real, new_u, new_hash});
            // HACK: Store pos map temporarily if needed, but candidates
            // copies are expensive. Better: Recompute pos map only for kept
            // candidates? Actually, we need it for next_beam. Let's add
            // pos_map to ChildCand or recompute later. For now, let's
            // optimize: don't store map in ChildCand to save sorting copy
            // time. Recompute it when pushing to next_beam. WAIT!
            // Recomputing is O(N^2) or O(k^2). O(k^2) is fast.
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
        global_visited.insert(cand.hash);
        vector<Rotation> new_path = current.path;
        new_path.emplace_back(cand.k, cand.i, cand.j);

        // Regenerate the pos map efficiently using incremental logic
        // This avoids storing it in the huge candidates vector
        auto update = calculate_incremental_update(
            current.unified_score, current.val_positions, cand.k, cand.i,
            cand.j, current.grid);

        next_beam.push({cand.grid, new_path, cand.real_score,
                        cand.unified_score, cand.hash, update.second});
        if (cand.real_score > best_score_this_level)
          best_score_this_level = cand.real_score;
      }
    }

    if (depth % 10 == 0) {
      cout << "Depth " << depth << " best: " << best_score_this_level << "\n";
    }

    int kept = 0;
    while (!next_beam.empty() && kept < beam_width) {
      beam.push(next_beam.top());
      next_beam.pop();
      kept++;
    }
  }

  return best.path;
}

vector<Rotation> beam_search(const vector<int> &initial_grid) {
  int target = n * n / 2;

  // Stage 1: Fast & Strict (Width 300, Depth 72)
  cout << "--- Stage 1: Fast Strict Search ---\n";
  vector<Rotation> path = perform_beam_run(initial_grid, 300, target);
  vector<int> res = apply_rotations(initial_grid, path);
  if (is_solved(res))
    return path;

  vector<Rotation> best_path = path;
  int best_score = count_adjacent_pairs(res);

  if (is_time_up())
    return best_path;

  // Stage 2: Medium & Strict (Width 600, Depth 72)
  cout << "--- Stage 2: Medium Strict Search ---\n";
  path = perform_beam_run(initial_grid, 600, target);
  res = apply_rotations(initial_grid, path);
  int current_score = count_adjacent_pairs(res);
  if (current_score > best_score) {
    best_score = current_score;
    best_path = path;
  }
  if (is_solved(res))
    return path;

  if (is_time_up())
    return best_path;

  // Stage 3: Ultra-Wide Deterministic Fallback (+10% buffer)
  // Replaces Randomized Loop with a massive single deterministic search
  // Enabled by the huge speedup from 1D vector optimization
  // incase stage 2 doesn't find solution so this will be the last lifeline
  cout << "--- Stage 3: Ultra-Wide Deterministic Search (Width 2500, Depth "
          ") "
       << buffer << "---\n";
  path = perform_beam_run(initial_grid, 2500, buffer);

  return path;
}

vector<int> apply_rotations(vector<int> grid, const vector<Rotation> &path) {
  for (const auto &rot : path) {
    grid = rotate_submatrix(grid, rot.k, rot.i, rot.j);
  }
  return grid;
}

vector<Rotation> optimize_path(const vector<int> &start_grid,
                               vector<Rotation> path) {
  bool improved = true;
  while (improved) {
    improved = false;
    for (size_t i = 0; i < path.size(); ++i) {
      vector<Rotation> next_path = path;
      next_path.erase(next_path.begin() + i);
      if (is_solved(apply_rotations(start_grid, next_path))) {
        path = next_path;
        improved = true;
        break;
      }
    }
  }
  return path;
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
  int grid_size = size_grid;

  vector<int> grid = gen_rand_grid(grid_size);
  n = grid_size;

  cout << "Initial grid:\n";
  print_grid(grid);
  cout << "\nInitial pairs: " << count_adjacent_pairs(grid) << "/"
       << (n * n / 2) << "\n\n";

  start_time = high_resolution_clock::now();

  vector<Rotation> solution = beam_search(grid);

  int original_moves = solution.size();
  solution = optimize_path(grid, solution);
  int optimized_moves = solution.size();
  if (optimized_moves < original_moves) {
    cout << "Optimized path: " << original_moves << " -> " << optimized_moves
         << " moves.\n";
  }

  auto elapsed =
      duration<double>(high_resolution_clock::now() - start_time).count();

  vector<int> final_grid = apply_rotations(grid, solution);
  int final_score = count_adjacent_pairs(final_grid);

  cout << "\n=== RESULTS ===\n";
  cout << "Grid: " << n << "x" << n << "\n";
  cout << "Moves: " << solution.size() << " / " << (n * n / 2)
       << " (optimal)\n";
  cout << "Score: " << final_score << "/" << (n * n / 2) << "\n";
  cout << "Solved: " << (is_solved(final_grid) ? "YES" : "NO") << "\n";
  cout << "Time: " << fixed << setprecision(2) << elapsed << "s\n";

  bool strict_satisfied = (solution.size() <= n * n / 2);
  bool relaxed_satisfied = (solution.size() <= 80);

  if (strict_satisfied) {
    cout << "✓ Strict Constraint satisfied\n";
  } else if (relaxed_satisfied) {
    cout << "✓ 10% Buffer Constraint satisfied (<=\n" << buffer << ")";
  } else {
    cout << "✗ Constraint satisfied\n";
  }

  return 0;
}