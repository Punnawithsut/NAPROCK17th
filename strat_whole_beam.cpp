#include <iostream>
#include <vector>
#include <queue>
#include <unordered_set>
#include <cmath>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <omp.h>
#include <mutex>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <random>

using namespace std;
using namespace std::chrono;
using json = nlohmann::json;

struct Rotation
{
    int k, i, j;
    Rotation(int k, int i, int j) : k(k), i(i), j(j) {}
};

vector<vector<int>> get_random_board(int n)
{
    try {
        json requestBody = {
            {"boardSize", n}};

        cout << "Requesting board from API..." << endl;
        
        cpr::Response r = cpr::Post(
            cpr::Url{"https://naprock-server.vercel.app/getBoard"},
            cpr::Body{requestBody.dump()},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Timeout{10000}
        );

        cout << "API Response Status: " << r.status_code << endl;
        
        if (r.status_code != 200) {
            cerr << "API Error! Status code: " << r.status_code << endl;
            cerr << "Response: " << r.text << endl;
            throw runtime_error("Failed to get board from API");
        }

        cout << "Parsing JSON response..." << endl;
        json responseData = json::parse(r.text);
        
        if (!responseData.contains("board")) {
            cerr << "Response doesn't contain 'board' field!" << endl;
            cerr << "Response: " << r.text << endl;
            throw runtime_error("Invalid API response");
        }
        
        vector<vector<int>> board(n, vector<int>(n));

        for (int i = 0; i < n; ++i)
        {
            for (int j = 0; j < n; ++j)
            {
                board[i][j] = responseData["board"][i][j].get<int>();
            }
        }

        cout << "Board successfully loaded from API!" << endl;
        return board;
        
    } catch (const exception& e) {
        cerr << "Exception in get_random_board: " << e.what() << endl;
        cerr << "Falling back to local random generation..." << endl;
        
        srand(time(0) ^ rand());
        int assignedValues = n * n / 2;

        vector<int> values(assignedValues, 2);
        vector<vector<int>> grid(n, vector<int>(n));

        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                bool assigned = false;
                while (!assigned) {
                    int value = rand() % assignedValues;
                    if (values[value] > 0) {
                        grid[i][j] = value;
                        values[value]--;
                        assigned = true;
                    }
                }
            }
        }
        cout << "Fallback board generated successfully!" << endl;
        return grid;
    }
}

struct GridState
{
    vector<vector<int>> grid;
    vector<Rotation> path;
    int paired_count;
    int heuristic;

    bool operator<(const GridState &other) const
    {
        if (paired_count != other.paired_count) {
            return paired_count < other.paired_count;
        }
        return heuristic > other.heuristic;
    }
};

int n;
auto start_time = high_resolution_clock::now();
const double TIME_LIMIT = 270.0;

bool is_time_up()
{
    auto now = high_resolution_clock::now();
    duration<double> elapsed = now - start_time;
    return elapsed.count() >= TIME_LIMIT;
}

vector<vector<int>> gen_rand_grid(int n) {
    srand(time(0) ^ rand());
    int assignedValues = n * n / 2;

    vector<int> values(assignedValues, 2);
    vector<vector<int>> grid(n, vector<int>(n));

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            bool assigned = false;
            while (!assigned) {
                int value = rand() % assignedValues;
                if (values[value] > 0) {
                    grid[i][j] = value;
                    values[value]--;
                    assigned = true;
                }
            }
        }
    }
    return grid;
}

void print_grid(const vector<vector<int>> &grid)
{
    cout << "Grid size: " << grid.size() << "x" << (grid.empty() ? 0 : grid[0].size()) << endl;
    for (const auto &row : grid)
    {
        for (int val : row)
        {
            cout << setw(3) << val << " ";
        }
        cout << endl;
    }
}

int count_paired_values(const vector<vector<int>> &grid)
{
    int total_values = n * n / 2;
    vector<vector<pair<int, int>>> coords(total_values);
    
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
        {
            int val = grid[i][j];
            if (val < 0 || val >= total_values) {
                cerr << "Invalid value " << val << " at position (" << i << "," << j << ")" << endl;
                cerr << "Expected values in range [0, " << total_values-1 << "]" << endl;
                throw runtime_error("Invalid grid value");
            }
            coords[val].push_back({i, j});
        }
    }
    
    for (int v = 0; v < total_values; v++) {
        if (coords[v].size() != 2) {
            cerr << "Value " << v << " appears " << coords[v].size() << " times (should be 2)" << endl;
            throw runtime_error("Invalid grid: values must appear exactly twice");
        }
    }
    
    int paired_count = 0;
    for (int v = 0; v < total_values; v++)
    {
        auto &p1 = coords[v][0];
        auto &p2 = coords[v][1];
        int d = abs(p1.first - p2.first) + abs(p1.second - p2.second);
        if (d == 1) {
            paired_count++;
        }
    }
    
    return paired_count;
}

int calculate_manhattan_heuristic(const vector<vector<int>> &grid)
{
    int total_values = n * n / 2;
    vector<vector<pair<int, int>>> coords(total_values);
    
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
        {
            int val = grid[i][j];
            coords[val].push_back({i, j});
        }
    }
    
    int total_distance = 0;
    for (int v = 0; v < total_values; v++)
    {
        auto &p1 = coords[v][0];
        auto &p2 = coords[v][1];
        int d = (abs(p1.first - p2.first) + abs(p1.second - p2.second))+ max(abs(p1.first - p2.first) , abs(p1.second - p2.second));
        total_distance += d;
    }

    return total_distance;
}

bool is_solved(const vector<vector<int>> &grid)
{
    int target = n * n / 2;
    return count_paired_values(grid) == target;
}

vector<vector<int>> rotate_submatrix(const vector<vector<int>> &grid, int k, int i, int j)
{
    vector<vector<int>> new_grid = grid;
    for (int x = 0; x < k; ++x)
    {
        for (int y = 0; y < k; ++y)
        {
            new_grid[i + y][j + k - 1 - x] = grid[i + x][j + y];
        }
    }
    return new_grid;
}

string get_grid_key(const vector<vector<int>> &grid)
{
    string key;
    for (const auto &row : grid)
    {
        for (int val : row)
        {
            key += to_string(val) + ',';
        }
    }
    return key;
}

vector<GridState> unstuck_healing(const GridState& stuck_state, int num_random_moves)
{
    cout << "HEALING: Applying " << num_random_moves << " random moves to escape local optimum..." << endl;
    
    vector<GridState> healed_states;
    random_device rd;
    mt19937 gen(rd());
    
    for (int attempt = 0; attempt < 10; attempt++) {
        vector<vector<int>> current_grid = stuck_state.grid;
        vector<Rotation> current_path = stuck_state.path;
        
        for (int move = 0; move < num_random_moves; move++) {
            uniform_int_distribution<> k_dist(2, min(8, n));
            int k = k_dist(gen);
            
            uniform_int_distribution<> i_dist(0, n - k);
            int i = i_dist(gen);
            
            uniform_int_distribution<> j_dist(0, n - k);
            int j = j_dist(gen);
            
            current_grid = rotate_submatrix(current_grid, k, i, j);
            current_path.emplace_back(k, i, j);
        }
        
        int new_paired = count_paired_values(current_grid);
        int new_heuristic = calculate_manhattan_heuristic(current_grid);
        
        healed_states.push_back({current_grid, current_path, new_paired, new_heuristic});
    }
    
    sort(healed_states.begin(), healed_states.end(), [](const GridState& a, const GridState& b) {
        if (a.paired_count != b.paired_count) {
            return a.paired_count > b.paired_count;
        }
        return a.heuristic < b.heuristic;
    });
    
    cout << "HEALING: Best healed state has " << healed_states[0].paired_count << " pairs" << endl;
    
    return healed_states;
}

// State snapshot for backtracking
struct StateSnapshot {
    vector<GridState> beam_states;
    int paired_count;
    int depth_at_snapshot;
    double beam_multiplier;
};

vector<Rotation> beam_search(const vector<vector<int>> &initial_grid, int max_depth)
{
    int initial_paired = count_paired_values(initial_grid);
    int initial_heuristic = calculate_manhattan_heuristic(initial_grid);
    int target_paired = n * n / 2;

    int base_beam_width = 92160 / (n * n);
    double beam_multiplier = 1.0;
    
    int num_threads = omp_get_max_threads();
    cout << "Starting Parallel Beam Search (base beam width: " << base_beam_width << ", threads: " << num_threads << ")\n";
    cout << "Initial paired count: " << initial_paired << "/" << target_paired << "\n";

    priority_queue<GridState> beam;
    beam.push({initial_grid, {}, initial_paired, initial_heuristic});

    GridState global_best = {initial_grid, {}, initial_paired, initial_heuristic};
    int depth = 0;
    int stuck_counter = 0;
    int last_best_paired = initial_paired;
    
    // State history for backtracking
    vector<StateSnapshot> state_history;

    for (; depth < max_depth && !beam.empty(); ++depth)
    {
        if (is_time_up())
        {
            cout << "Time limit reached at depth " << depth << endl;
            return global_best.path;
        }

        vector<GridState> current_states;
        while (!beam.empty())
        {
            current_states.push_back(beam.top());
            beam.pop();
        }

        vector<vector<GridState>> thread_local_states(num_threads);
        vector<unordered_set<string>> thread_local_visited(num_threads);
        
        int best_paired_in_depth = 0;
        mutex best_mutex;
        mutex solution_mutex;
        bool solution_found = false;
        vector<Rotation> solution_path;

        #pragma omp parallel
        {
            int thread_id = omp_get_thread_num();
            int local_best_paired = 0;
            
            #pragma omp for schedule(dynamic)
            for (int idx = 0; idx < current_states.size(); idx++)
            {
                if (solution_found) continue;
                
                GridState current = current_states[idx];

                if (current.paired_count > global_best.paired_count)
                {
                    #pragma omp critical
                    {
                        if (current.paired_count > global_best.paired_count)
                        {
                            global_best = current;
                        }
                    }
                }
                
                if (is_solved(current.grid))
                {
                    #pragma omp critical(solution)
                    {
                        if (!solution_found)
                        {
                            solution_found = true;
                            solution_path = current.path;
                        }
                    }
                    continue;
                }

                for (int k = 2; k <= min(8, n); ++k)
                {
                    if (solution_found) break;
                    
                    for (int i = 0; i <= n - k; ++i)
                    {
                        if (solution_found) break;
                        
                        for (int j = 0; j <= n - k; ++j)
                        {
                            if (solution_found) break;
                            
                            vector<vector<int>> new_grid = rotate_submatrix(current.grid, k, i, j);
                            int new_paired = count_paired_values(new_grid);
                            
                            if (new_paired < current.paired_count) {
                                continue;
                            }
                            
                            int new_heuristic = calculate_manhattan_heuristic(new_grid);
                            vector<Rotation> new_path = current.path;
                            new_path.emplace_back(k, i, j);

                            if (new_paired > local_best_paired)
                            {
                                local_best_paired = new_paired;
                            }
                            
                            if (is_solved(new_grid))
                            {
                                #pragma omp critical(solution)
                                {
                                    if (!solution_found)
                                    {
                                        solution_found = true;
                                        solution_path = new_path;
                                    }
                                }
                                break;
                            }

                            string grid_key = get_grid_key(new_grid);
                            if (thread_local_visited[thread_id].find(grid_key) == thread_local_visited[thread_id].end())
                            {
                                thread_local_visited[thread_id].insert(grid_key);
                                thread_local_states[thread_id].push_back({new_grid, new_path, new_paired, new_heuristic});
                            }
                        }
                    }
                }
            }
            
            #pragma omp critical(best)
            {
                if (local_best_paired > best_paired_in_depth)
                {
                    best_paired_in_depth = local_best_paired;
                }
            }
        }

        if (solution_found)
        {
            cout << "Solution found at depth " << depth << endl;
            return solution_path;
        }

        cout << "Depth " << depth << ": processed " << current_states.size() 
             << " states, best paired: " << best_paired_in_depth << "/" << target_paired;
        
        if (best_paired_in_depth > last_best_paired) {
            cout << " (IMPROVED!)";
            
            // Save snapshot when we make progress
            StateSnapshot snapshot;
            snapshot.beam_states = current_states;
            snapshot.paired_count = best_paired_in_depth;
            snapshot.depth_at_snapshot = depth;
            snapshot.beam_multiplier = beam_multiplier;
            state_history.push_back(snapshot);
            
            last_best_paired = best_paired_in_depth;
            stuck_counter = 0;
        } else {
            stuck_counter++;
            cout << " (stuck: " << stuck_counter << "/5)";
        }
        cout << "\n";

        priority_queue<GridState> next_beam;
        unordered_set<string> global_visited;
        
        for (int t = 0; t < num_threads; t++)
        {
            for (const auto& state : thread_local_states[t])
            {
                string key = get_grid_key(state.grid);
                if (global_visited.find(key) == global_visited.end())
                {
                    global_visited.insert(key);
                    next_beam.push(state);
                }
            }
        }

        // BACKTRACKING MECHANISM: If stuck for more than 4 times, revert to previous state
        if (stuck_counter >= 5) {
            if (!state_history.empty()) {
                cout << "\n!!! BACKTRACKING TRIGGERED !!!" << endl;
                cout << "Stuck at " << best_paired_in_depth << " pairs for " << stuck_counter << " iterations" << endl;
                
                // Get the previous state
                StateSnapshot& prev_snapshot = state_history.back();
                
                cout << "Reverting to previous state with " << prev_snapshot.paired_count 
                     << " pairs (depth " << prev_snapshot.depth_at_snapshot << ")" << endl;
                
                // Increase beam width by 1.5x
                beam_multiplier = prev_snapshot.beam_multiplier * 1.5;
                cout << "Increasing beam width multiplier to " << beam_multiplier 
                     << " (effective width: " << (int)(base_beam_width * beam_multiplier) << ")" << endl;
                
                // Restore the beam with previous states
                next_beam = priority_queue<GridState>();
                for (const auto& state : prev_snapshot.beam_states) {
                    next_beam.push(state);
                }
                
                // Update the snapshot with new multiplier
                state_history.back().beam_multiplier = beam_multiplier;
                
                // Reset stuck counter
                stuck_counter = 0;
                last_best_paired = prev_snapshot.paired_count;
                
                cout << "Backtracking complete. Continuing search with expanded beam...\n" << endl;
                
            } else {
                // No history to backtrack to, use healing instead
                cout << "STUCK DETECTED! No history to backtrack. Triggering healing mechanism..." << endl;
                
                GridState best_current = current_states[0];
                for (const auto& state : current_states) {
                    if (state.paired_count > best_current.paired_count) {
                        best_current = state;
                    }
                }
                
                int num_random_moves = min(10, n / 2);
                vector<GridState> healed = unstuck_healing(best_current, num_random_moves);
                
                for (const auto& healed_state : healed) {
                    next_beam.push(healed_state);
                }
                
                stuck_counter = 0;
            }
        }

        // Apply beam width with multiplier
        int current_beam_width = (int)(base_beam_width * beam_multiplier);
        
        beam = priority_queue<GridState>();
        int kept = 0;
        while (!next_beam.empty() && kept < current_beam_width)
        {
            GridState state = next_beam.top();
            if (state.paired_count >= best_paired_in_depth - 1)
            {
                beam.push(state);
                kept++;
            }
            next_beam.pop();
        }
        
        if (kept == 0) {
            cout << "No improving moves found. Search terminated at depth " << depth << "\n";
            break;
        }
        
        cout << "Kept " << kept << " states for next depth (beam width: " << current_beam_width << ")\n";
    }
    
    cout << "Beam search completed to depth " << depth << endl;
    cout << "Best paired count achieved: " << global_best.paired_count << "/" << target_paired << "\n";
    return global_best.path;
}

vector<vector<int>> apply_rotations(vector<vector<int>> grid, const vector<Rotation> &path)
{
    for (const auto &rot : path)
    {
        grid = rotate_submatrix(grid, rot.k, rot.i, rot.j);
    }
    return grid;
}

int main() {
    int T = 1;
    int d = 12;

    omp_set_num_threads(omp_get_max_threads());
    cout << "Using " << omp_get_max_threads() << " OpenMP threads\n";

    vector<vector<Rotation>> all_solutions;
    all_solutions.reserve(T);

    int success_count = 0;
    int max_moves = 0;
    double total_moves = 0.0;
    double total_time = 0.0;

    for (int t = 0; t < T; t++) {
        cout << "\n=== Test Case " << (t + 1) << " ===\n";
        
        try {
            vector<vector<int>> grid = get_random_board(d);
            n = d;

            cout << "\n--- Initial Board ---\n";
            print_grid(grid);

            start_time = high_resolution_clock::now();

            int max_search_depth = n * n * 2;
            cout << "\nMax search depth: " << max_search_depth << endl;
            
            vector<Rotation> solution_path = beam_search(grid, max_search_depth);

            vector<vector<int>> final_grid = apply_rotations(grid, solution_path);
            int final_paired = count_paired_values(final_grid);

            duration<double> elapsed = high_resolution_clock::now() - start_time;
            double time_used = elapsed.count();

            int moves = solution_path.size();
            bool solved = is_solved(final_grid);
            
            cout << "\n--- Results ---\n";
            cout << "Solved: " << (solved ? "YES" : "NO") << "\n";
            cout << "Paired count: " << final_paired << "/" << (n*n/2) << "\n";
            cout << "Moves: " << moves << "\n";
            cout << "Time: " << time_used << " seconds\n";

            if (solved) {
                success_count++;
                total_moves += moves;
                max_moves = max(max_moves, moves);
            }
            total_time += time_used;

            all_solutions.push_back(solution_path);
            
        } catch (const exception& e) {
            cerr << "\n!!! ERROR in test case " << (t + 1) << ": " << e.what() << endl;
            total_time += 0;
        }
    }

    cout << fixed << setprecision(5);
    cout << "\n=== Final Summary ===\n";
    cout << d << "x" << d << " " << T << " testcase(s)\n";
    cout << "Success rate: " << (double)success_count / T << "\n";
    if (success_count > 0) {
        cout << "Avg Moves: " << total_moves / success_count << "\n";
    } else {
        cout << "Avg Moves: N/A\n";
    }
    cout << "Avg time: " << total_time / T << " seconds\n";
    cout << "Max moves: " << max_moves << "\n";

    return 0;
}