#include <iostream>
#include <vector>
#include <queue>
#include <unordered_set>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <random>
#include <string>

using namespace std;
using namespace std::chrono;

int n;
auto start_time = high_resolution_clock::now();
const double TIME_LIMIT = 270.0;

struct Rotation {
    int k, i, j;
    Rotation(int k, int i, int j) : k(k), i(i), j(j) {}
};

struct GridState {
    vector<vector<int>> grid;
    vector<Rotation> path;
    int score;
    double heuristic;
    
    bool operator<(const GridState &other) const {
        if (score != other.score) return score < other.score;
        return heuristic < other.heuristic;
    }
};

vector<vector<int>> gen_rand_grid(int n) {
    srand(time(0));
    int num_values = n * n / 2;
    vector<int> values(num_values, 2);
    vector<vector<int>> grid(n, vector<int>(n));
    
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            bool assigned = false;
            while (!assigned) {
                int idx = rand() % num_values;
                if (values[idx] > 0) {
                    grid[i][j] = idx + 1;
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

int count_adjacent_pairs(const vector<vector<int>> &grid) {
    int count = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i + 1 < n && grid[i][j] == grid[i + 1][j]) count++;
            if (j + 1 < n && grid[i][j] == grid[i][j + 1]) count++;
        }
    }
    return count;
}

// Lighter heuristic - just count pairs within distance 2-3
double calculate_heuristic(const vector<vector<int>> &grid) {
    int num_values = n * n / 2;
    vector<vector<pair<int,int>>> positions(num_values + 1);
    
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            positions[grid[i][j]].push_back({i, j});
        }
    }
    
    double score = 0;
    for (int v = 1; v <= num_values; v++) {
        auto p1 = positions[v][0];
        auto p2 = positions[v][1];
        int dist = abs(p1.first - p2.first) + abs(p1.second - p2.second);
        
        if (dist == 1) score += 10.0;  // Adjacent
        else if (dist == 2) score += 3.0;  // Close
        else if (dist == 3) score += 1.0;  // Nearby
        else score -= dist * 0.1;  // Small penalty for far pairs
    }
    
    return score;
}

bool is_solved(const vector<vector<int>> &grid) {
    return count_adjacent_pairs(grid) == n * n / 2;
}

vector<vector<int>> rotate_submatrix(const vector<vector<int>> &grid, int k, int i, int j) {
    vector<vector<int>> new_grid = grid;
    for (int x = 0; x < k; x++) {
        for (int y = 0; y < k; y++) {
            new_grid[i + y][j + k - 1 - x] = grid[i + x][j + y];
        }
    }
    return new_grid;
}

string get_grid_key(const vector<vector<int>> &grid) {
    string key;
    for (const auto &row : grid) {
        for (int val : row) {
            key += to_string(val) + ',';
        }
    }
    return key;
}

vector<Rotation> beam_search(const vector<vector<int>> &initial_grid) {
    int target_score = n * n / 2;
    int max_depth = target_score;
    
    // More generous beam width
    int beam_width;
    if (n <= 8) beam_width = 800;
    else if (n <= 10) beam_width = 300;
    else if (n <= 12) beam_width = 150;
    else beam_width = 100;
    
    cout << "Grid: " << n << "x" << n << ", Target: " << target_score << "\n";
    cout << "Beam width: " << beam_width << ", Max depth: " << max_depth << "\n";
    
    priority_queue<GridState> beam;
    double init_h = calculate_heuristic(initial_grid);
    beam.push({initial_grid, {}, count_adjacent_pairs(initial_grid), init_h});
    
    GridState best = {initial_grid, {}, count_adjacent_pairs(initial_grid), init_h};
    unordered_set<string> global_visited;
    global_visited.insert(get_grid_key(initial_grid));
    
    for (int depth = 0; depth < max_depth && !beam.empty(); depth++) {
        if (is_time_up()) {
            cout << "Time limit at depth " << depth << "\n";
            break;
        }
        
        priority_queue<GridState> next_beam;
        int best_score = -1;
        int states_explored = 0;
        
        // Process all states in current beam
        vector<GridState> current_states;
        while (!beam.empty()) {
            current_states.push_back(beam.top());
            beam.pop();
        }
        
        for (const auto& current : current_states) {
            if (current.score > best.score) {
                best = current;
            }
            
            if (is_solved(current.grid)) {
                cout << "SOLVED at depth " << depth << " with " << current.path.size() << " moves\n";
                return current.path;
            }
            
            // Try rotations with some sampling for large grids
            int max_k = min(n, 7);
            
            // For large grids, randomly sample positions
            vector<tuple<int,int,int>> moves;
            for (int k = 2; k <= max_k; k++) {
                for (int i = 0; i <= n - k; i++) {
                    for (int j = 0; j <= n - k; j++) {
                        moves.push_back({k, i, j});
                    }
                }
            }
            
            // If too many moves, randomly sample
            if (moves.size() > 200 && n > 10) {
                random_device rd;
                mt19937 g(rd());
                shuffle(moves.begin(), moves.end(), g);
                moves.resize(200);
            }
            
            for (const auto& [k, i, j] : moves) {
                vector<vector<int>> new_grid = rotate_submatrix(current.grid, k, i, j);
                string key = get_grid_key(new_grid);
                
                if (global_visited.find(key) != global_visited.end()) {
                    continue;
                }
                
                int new_score = count_adjacent_pairs(new_grid);
                
                if (new_score > best_score) best_score = new_score;
                
                if (new_score == target_score) {
                    vector<Rotation> solution = current.path;
                    solution.emplace_back(k, i, j);
                    cout << "SOLVED at depth " << depth << " with " << solution.size() << " moves\n";
                    return solution;
                }
                
                // More lenient acceptance - don't filter too much
                if (new_score >= current.score - 2 || depth < 5) {
                    global_visited.insert(key);
                    double new_h = calculate_heuristic(new_grid);
                    vector<Rotation> new_path = current.path;
                    new_path.emplace_back(k, i, j);
                    next_beam.push({new_grid, new_path, new_score, new_h});
                    states_explored++;
                }
            }
        }
        
        cout << "Depth " << depth << ": explored=" << states_explored 
             << " best=" << best_score << "/" << target_score << "\n";
        
        // Keep diverse set of top states
        beam = priority_queue<GridState>();
        vector<GridState> candidates;
        
        while (!next_beam.empty()) {
            candidates.push_back(next_beam.top());
            next_beam.pop();
        }
        
        // Sort by score primarily
        sort(candidates.begin(), candidates.end(), [](const GridState& a, const GridState& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.heuristic > b.heuristic;
        });
        
        // Keep top states
        int keep_count = min(beam_width, (int)candidates.size());
        for (int i = 0; i < keep_count; i++) {
            beam.push(candidates[i]);
        }
        
        if (beam.empty()) {
            cout << "Beam empty at depth " << depth << "\n";
            break;
        }
    }
    
    cout << "Best: " << best.path.size() << " moves, score=" << best.score << "/" << target_score << "\n";
    return best.path;
}

vector<vector<int>> apply_rotations(vector<vector<int>> grid, const vector<Rotation> &path) {
    for (const auto &rot : path) {
        grid = rotate_submatrix(grid, rot.k, rot.i, rot.j);
    }
    return grid;
}

void print_grid(const vector<vector<int>> &grid) {
    for (const auto &row : grid) {
        for (int val : row) {
            cout << setw(2) << val << " ";
        }
        cout << "\n";
    }
}

int main() {
    int grid_size = 10;
    
    vector<vector<int>> grid = gen_rand_grid(grid_size);
    n = grid_size;
    
    cout << "Initial grid:\n";
    print_grid(grid);
    cout << "\nInitial pairs: " << count_adjacent_pairs(grid) << "/" << (n*n/2) << "\n\n";
    
    start_time = high_resolution_clock::now();
    vector<Rotation> solution = beam_search(grid);
    
    auto elapsed = duration<double>(high_resolution_clock::now() - start_time).count();
    
    vector<vector<int>> final_grid = apply_rotations(grid, solution);
    int final_score = count_adjacent_pairs(final_grid);
    
    cout << "\n=== RESULTS ===\n";
    cout << "Grid: " << n << "x" << n << "\n";
    cout << "Moves: " << solution.size() << " / " << (n*n/2) << " (limit)\n";
    cout << "Score: " << final_score << "/" << (n*n/2) << "\n";
    cout << "Solved: " << (is_solved(final_grid) ? "YES" : "NO") << "\n";
    cout << "Time: " << fixed << setprecision(2) << elapsed << "s\n";
    cout << (solution.size() <= n*n/2 ? "✓" : "✗") << " Constraint satisfied\n";
    
    return 0;
}
