#include <iostream>
#include <vector>
#include <queue>
#include <unordered_set>
#include <cmath>
#include <ctime>
#include <chrono>
#include <random>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <functional>
#include <cassert>

using namespace std;
using namespace std::chrono;

/* ===================== Random grid generator ===================== */
vector<vector<int>> gen_rand_grid(int n)
{
    srand((unsigned)time(0) ^ rand());
    int assignedValues = (n * n) / 2;

    vector<int> values(assignedValues, 2);
    vector<vector<int>> grid(n, vector<int>(n));

    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
        {
            bool assigned = false;
            while (!assigned)
            {
                int value = rand() % assignedValues;
                if (values[value] > 0)
                {
                    grid[i][j] = value + 1;
                    values[value]--;
                    assigned = true;
                }
            }
        }
    }
    return grid;
}

/* ===================== xoshiro256** RNG ===================== */
class xoshiro256ss
{
public:
    using result_type = uint64_t;

    explicit xoshiro256ss(uint64_t seed = 0)
    {
        s[0] = splitmix64(seed);
        s[1] = splitmix64(s[0]);
        s[2] = splitmix64(s[1]);
        s[3] = splitmix64(s[2]);
    }

    static constexpr uint64_t min() { return 0ULL; }
    static constexpr uint64_t max() { return UINT64_MAX; }

    uint64_t operator()()
    {
        uint64_t *ss = s;
        uint64_t const result = rotl(ss[1] * 5, 7) * 9;
        uint64_t const t = ss[1] << 17;

        ss[2] ^= ss[0];
        ss[3] ^= ss[1];
        ss[1] ^= ss[2];
        ss[0] ^= ss[3];

        ss[2] ^= t;
        ss[3] = rotl(ss[3], 45);

        return result;
    }

private:
    static inline uint64_t rotl(uint64_t x, int k)
    {
        return (x << k) | (x >> (64 - k));
    }

    static uint64_t splitmix64(uint64_t x)
    {
        uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    uint64_t s[4];
};

/* ===================== Core structs & globals ===================== */
struct Rotation
{
    int k, i, j;
    Rotation(int k, int i, int j) : k(k), i(i), j(j) {}
};

struct GridState
{
    vector<vector<int>> grid;
    vector<Rotation> path;
    float score; // Combined score with multiple factors
    int adjacent_pairs;
    float efficiency; // Score per move ratio

    bool operator<(const GridState &other) const
    {
        if (abs(score - other.score) < 1e-6)
        {
            return efficiency < other.efficiency; // Prefer higher efficiency
        }
        return score < other.score; // For max-heap (higher score is better)
    }
};

int n;
auto start_time_global = high_resolution_clock::now();
const double TIME_LIMIT = 270.0; // 4.5 minutes

// Early-SA trigger state
bool g_trigger_SA = false;
vector<vector<int>> g_beam_best_grid;
vector<Rotation> g_beam_best_path;

/* ===================== Helpers ===================== */
bool is_time_up()
{
    auto now = high_resolution_clock::now();
    duration<double> elapsed = now - start_time_global;
    return elapsed.count() >= TIME_LIMIT;
}

void print_grid(const vector<vector<int>> &grid)
{
    for (const auto &row : grid)
    {
        for (int val : row)
            cout << setw(2) << val << " ";
        cout << '\n';
    }
}

int count_adjacent_pairs(const vector<vector<int>> &grid)
{
    int count = 0;
    for (int i = 0; i < n; ++i)
    {
        for (int j = 0; j < n; ++j)
        {
            if (i + 1 < n && grid[i][j] == grid[i + 1][j])
                count++;
            if (j + 1 < n && grid[i][j] == grid[i][j + 1])
                count++;
        }
    }
    return count;
}

// Enhanced scoring function that considers multiple factors
float calculate_enhanced_score(const vector<vector<int>> &grid, int path_length = 0)
{
    int adjacent_pairs = count_adjacent_pairs(grid);
    int target_pairs = n * n / 2;

    // Base score from adjacent pairs
    float score = (float)adjacent_pairs;

    // Bonus for clustering - pairs that are close together get higher score
    float clustering_bonus = 0.0f;
    unordered_map<int, vector<pair<int, int>>> positions;

    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
        {
            positions[grid[i][j]].push_back({i, j});
        }
    }

    for (const auto &[value, coords] : positions)
    {
        if (coords.size() == 2)
        {
            int dist = abs(coords[0].first - coords[1].first) + abs(coords[0].second - coords[1].second);
            if (dist == 1)
            {
                clustering_bonus += 2.0f; // Already adjacent
            }
            else if (dist <= 3)
            {
                clustering_bonus += 1.5f / dist; // Close together
            }
            else
            {
                clustering_bonus += 0.5f / dist; // Farther apart
            }
        }
    }

    // Penalty for longer paths (encourage efficiency)
    float path_penalty = path_length > 0 ? path_length * 0.01f : 0.0f;

    return score + clustering_bonus - path_penalty;
}

bool is_solved(const vector<vector<int>> &grid)
{
    return count_adjacent_pairs(grid) == n * n / 2;
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

void rotate_submatrix_in_place(vector<vector<int>> &grid, int k, int i0, int j0)
{
    for (int a = 0; a < k / 2; a++)
    {
        for (int b = a; b < k - a - 1; b++)
        {
            int temp = grid[i0 + a][j0 + b];
            grid[i0 + a][j0 + b] = grid[i0 + k - 1 - b][j0 + a];
            grid[i0 + k - 1 - b][j0 + a] = grid[i0 + k - 1 - a][j0 + k - 1 - b];
            grid[i0 + k - 1 - a][j0 + k - 1 - b] = grid[i0 + b][j0 + k - 1 - a];
            grid[i0 + b][j0 + k - 1 - a] = temp;
        }
    }
}

string get_grid_key(const vector<vector<int>> &grid)
{
    string key;
    key.reserve(n * n * 3);
    for (const auto &row : grid)
    {
        for (int val : row)
        {
            key += to_string(val);
            key += ',';
        }
    }
    return key;
}

// Intelligent move ordering - prioritize moves that affect misplaced pairs
vector<tuple<int, int, int>> get_prioritized_moves(const vector<vector<int>> &grid)
{
    vector<tuple<int, int, int, float>> scored_moves;

    // Find all misplaced pairs and their locations
    unordered_map<int, vector<pair<int, int>>> positions;
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
        {
            positions[grid[i][j]].push_back({i, j});
        }
    }

    unordered_set<pair<int, int>, function<size_t(const pair<int, int> &)>> problem_areas(
        0, [](const pair<int, int> &p)
        { return hash<int>{}(p.first) ^ (hash<int>{}(p.second) << 1); });

    for (const auto &[value, coords] : positions)
    {
        if (coords.size() == 2)
        {
            int dist = abs(coords[0].first - coords[1].first) + abs(coords[0].second - coords[1].second);
            if (dist > 1)
            { // Not adjacent
                problem_areas.insert(coords[0]);
                problem_areas.insert(coords[1]);
            }
        }
    }

    // Generate moves with priority scoring
    for (int k = 2; k <= min(6, n - 1); ++k)
    { // Reduced max k for efficiency
        for (int i = 0; i <= n - k; ++i)
        {
            for (int j = 0; j <= n - k; ++j)
            {
                float priority = 0.0f;

                // Check if this move affects problem areas
                bool affects_problems = false;
                for (int x = i; x < i + k; x++)
                {
                    for (int y = j; y < j + k; y++)
                    {
                        if (problem_areas.count({x, y}))
                        {
                            affects_problems = true;
                            priority += 1.0f;
                        }
                    }
                }

                if (!affects_problems)
                {
                    priority = 0.1f; // Low priority for moves that don't help
                }

                // Prefer smaller rotations for efficiency
                priority += 1.0f / (k * k);

                scored_moves.emplace_back(k, i, j, priority);
            }
        }
    }

    // Sort by priority (highest first)
    sort(scored_moves.begin(), scored_moves.end(),
         [](const auto &a, const auto &b)
         { return get<3>(a) > get<3>(b); });

    // Convert to simple moves vector
    vector<tuple<int, int, int>> moves;
    for (const auto &move : scored_moves)
    {
        moves.emplace_back(get<0>(move), get<1>(move), get<2>(move));
    }

    return moves;
}

/* ===================== Enhanced Beam Search ===================== */
vector<Rotation> beam_search(const vector<vector<int>> &initial_grid, int max_depth, double time_budget)
{
    auto beam_start = high_resolution_clock::now();
    float initial_score = calculate_enhanced_score(initial_grid);
    int target_pairs = n * n / 2;

    // Adaptive beam width
    int beam_width = max(2, min(50, 8000 / (n * n)));
    cout << "Starting Enhanced Beam Search (beam width: " << beam_width << ")\n";
    cout << "Initial score: " << initial_score << " (target pairs: " << target_pairs << ")\n";

    priority_queue<GridState> beam;
    GridState initial_state = {initial_grid, {}, initial_score, count_adjacent_pairs(initial_grid), initial_score};
    beam.push(initial_state);

    GridState global_best = initial_state;
    int depth = 0;

    // MODIFIED: Track integer score (adjacent pairs) instead of float score
    int prev_best_integer_score = -1;
    int same_integer_score_streak = 0;
    const int PLATEAU_THRESHOLD = 10; // Trigger SA after 10 plateaus

    for (; depth < max_depth && !beam.empty(); ++depth)
    {
        priority_queue<GridState> next_beam;
        unordered_set<string> visited;
        int states_processed = 0;
        float best_score_in_depth = -1000.0f;
        int best_integer_score_in_depth = -1; // Track best integer score in this depth

        while (!beam.empty())
        {
            // Check time budget
            auto now_time = high_resolution_clock::now();
            double elapsed = duration<double>(now_time - beam_start).count();
            if (elapsed > time_budget || is_time_up())
            {
                cout << "Beam search time budget exceeded at depth " << depth << '\n';
                g_trigger_SA = true;
                g_beam_best_grid = global_best.grid;
                g_beam_best_path = global_best.path;
                return global_best.path;
            }

            GridState current = beam.top();
            beam.pop();
            states_processed++;

            if (current.score > global_best.score)
            {
                global_best = current;
            }
            if (is_solved(current.grid))
            {
                cout << "Solution found at depth " << depth << " with " << current.path.size() << " moves\n";
                return current.path;
            }

            // Use prioritized move ordering
            auto moves = get_prioritized_moves(current.grid);
            int moves_considered = 0;
            const int max_moves_per_state = min(200, (int)moves.size());

            for (const auto &[k, i, j] : moves)
            {
                if (moves_considered >= max_moves_per_state)
                    break;
                moves_considered++;

                vector<vector<int>> new_grid = rotate_submatrix(current.grid, k, i, j);
                int new_adjacent = count_adjacent_pairs(new_grid);

                // Quick pruning - skip moves that significantly worsen the state
                if (new_adjacent < current.adjacent_pairs - 2 && depth > 2)
                {
                    continue;
                }

                float new_score = calculate_enhanced_score(new_grid, current.path.size() + 1);
                vector<Rotation> new_path = current.path;
                new_path.emplace_back(k, i, j);

                float efficiency = new_path.empty() ? new_score : new_score / new_path.size();

                if (new_score > best_score_in_depth)
                {
                    best_score_in_depth = new_score;
                }

                // MODIFIED: Track best integer score in this depth
                if (new_adjacent > best_integer_score_in_depth)
                {
                    best_integer_score_in_depth = new_adjacent;
                }

                if (new_adjacent == target_pairs)
                {
                    cout << "Solution found at depth " << depth << " with " << new_path.size() << " moves\n";
                    return new_path;
                }

                string grid_key = get_grid_key(new_grid);
                if (visited.find(grid_key) == visited.end())
                {
                    visited.insert(grid_key);
                    GridState new_state = {std::move(new_grid), std::move(new_path), new_score, new_adjacent, efficiency};
                    next_beam.push(new_state);
                }
            }
        }

        cout << "Depth " << depth << ": processed " << states_processed
             << " states, best score: " << best_score_in_depth
             << ", best integer score: " << best_integer_score_in_depth << '\n';

        // MODIFIED: Enhanced early termination based on INTEGER score plateau
        if (best_integer_score_in_depth == prev_best_integer_score)
        {
            same_integer_score_streak++;
            cout << "Integer score plateau detected (" << same_integer_score_streak
                 << " times at score " << best_integer_score_in_depth << ")\n";

            if (same_integer_score_streak >= PLATEAU_THRESHOLD)
            {
                cout << "[Beam] Integer score plateaued for " << same_integer_score_streak
                     << " depths at score " << best_integer_score_in_depth
                     << " — triggering SA early.\n";
                g_trigger_SA = true;
                g_beam_best_grid = global_best.grid;
                g_beam_best_path = global_best.path;
                return global_best.path;
            }
        }
        else
        {
            same_integer_score_streak = 0;
            cout << "Integer score improved from " << prev_best_integer_score
                 << " to " << best_integer_score_in_depth << " - resetting plateau counter\n";
        }
        prev_best_integer_score = best_integer_score_in_depth;

        // Adaptive beam filtering
        beam = priority_queue<GridState>();
        vector<GridState> candidates;

        while (!next_beam.empty())
        {
            candidates.push_back(next_beam.top());
            next_beam.pop();
        }

        // Sort by score and efficiency
        sort(candidates.begin(), candidates.end(), [](const GridState &a, const GridState &b)
             {
                 if (abs(a.score - b.score) < 0.1f)
                 {
                     return a.efficiency > b.efficiency; // Higher efficiency first
                 }
                 return a.score > b.score; // Higher score first
             });

        int kept = 0;
        float score_threshold = candidates.empty() ? 0 : candidates[0].score - 1.0f;

        for (const auto &state : candidates)
        {
            if (kept >= beam_width)
                break;
            if (state.score >= score_threshold)
            {
                beam.push(state);
                kept++;
            }
        }

        cout << "Kept " << kept << " states for next depth\n";
    }

    cout << "Beam search completed to depth " << depth << '\n';

    g_trigger_SA = true;
    g_beam_best_grid = global_best.grid;
    g_beam_best_path = global_best.path;
    return global_best.path;
}

/* ===================== Enhanced Simulated Annealing ===================== */
vector<Rotation> simulated_annealing(vector<vector<int>> grid, const vector<Rotation> &initial_path)
{
    int total_values = (n * n) / 2;
    cout << "\nStarting Enhanced Simulated Annealing\n";

    vector<vector<pair<int, int>>> coords(total_values + 1);
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < n; j++)
        {
            int val = grid[i][j];
            coords[val].push_back({i, j});
        }
    }

    auto manhattan = [](const pair<int, int> &a, const pair<int, int> &b)
    {
        return abs(a.first - b.first) + abs(a.second - b.second);
    };

    int current_cost = 0;
    for (int v = 1; v <= total_values; v++)
    {
        auto &p1 = coords[v][0];
        auto &p2 = coords[v][1];
        current_cost += manhattan(p1, p2);
    }

    // Adaptive parameters based on current state
    int max_steps = min(50000, max(10000, current_cost * 1000));
    int num_candidates = min(400, max(150, n * n * 2));
    double T = max(2.0, 3.0 * sqrt(n));
    const double cooling_rate = 0.96;

    vector<Rotation> sequence;
    unsigned long long seed = (unsigned long long)chrono::steady_clock::now().time_since_epoch().count();
    xoshiro256ss rng(seed);

    // Enhanced move selection with focus on problem pairs
    vector<int> problem_values;
    for (int v = 1; v <= total_values; v++)
    {
        if (manhattan(coords[v][0], coords[v][1]) > 1)
        {
            problem_values.push_back(v);
        }
    }

    auto start_time = chrono::steady_clock::now();
    int improvements = 0;
    int last_improvement_step = 0;

    for (int step = 0; step < max_steps; step++)
    {
        if (current_cost == total_values)
            break;

        auto now = chrono::steady_clock::now();
        auto elapsed = chrono::duration_cast<chrono::seconds>(now - start_time).count();
        if (elapsed >= 270 || is_time_up())
            break;

        // Early termination if no improvement for too long
        if (step - last_improvement_step > max_steps / 4 && step > 5000)
        {
            cout << "SA: No improvement for " << (step - last_improvement_step) << " steps, terminating early\n";
            break;
        }

        vector<tuple<int, int, int, int, unordered_set<int>>> candidates;

        for (int c = 0; c < num_candidates; c++)
        {
            // Bias toward smaller k for efficiency, and focus on problem areas
            int k;
            if (!problem_values.empty() && rng() % 3 == 0)
            {
                // Target problem areas
                int prob_val = problem_values[rng() % problem_values.size()];
                auto &prob_coords = coords[prob_val];
                int center_i = (prob_coords[0].first + prob_coords[1].first) / 2;
                int center_j = (prob_coords[0].second + prob_coords[1].second) / 2;

                k = 2 + (rng() % min(4, n - 1)); // Prefer smaller k
                int i = max(0, min(n - k, center_i - k / 2));
                int j = max(0, min(n - k, center_j - k / 2));

                unordered_set<int> S;
                for (int a = 0; a < k; a++)
                    for (int b = 0; b < k; b++)
                        S.insert(grid[i + a][j + b]);

                int delta = 0;
                for (int val : S)
                {
                    auto &points = coords[val];
                    auto p1 = points[0], p2 = points[1];
                    int old_d = manhattan(p1, p2);

                    pair<int, int> np1 = p1, np2 = p2;
                    if (p1.first >= i && p1.first < i + k && p1.second >= j && p1.second < j + k)
                    {
                        int a1 = p1.first - i, b1 = p1.second - j;
                        np1 = {i + b1, j + (k - 1 - a1)};
                    }
                    if (p2.first >= i && p2.first < i + k && p2.second >= j && p2.second < j + k)
                    {
                        int a2 = p2.first - i, b2 = p2.second - j;
                        np2 = {i + b2, j + (k - 1 - a2)};
                    }
                    delta += (manhattan(np1, np2) - old_d);
                }
                candidates.emplace_back(k, i, j, delta, std::move(S));
            }
            else
            {
                // Random move with bias toward smaller k
                k = 2 + (rng() % min(5, n - 1));
                if (rng() % 3 == 0)
                    k = 2; // Extra bias toward 2x2

                uniform_int_distribution<int> i_dist(0, n - k);
                uniform_int_distribution<int> j_dist(0, n - k);
                int i = i_dist(rng);
                int j = j_dist(rng);

                unordered_set<int> S;
                for (int a = 0; a < k; a++)
                    for (int b = 0; b < k; b++)
                        S.insert(grid[i + a][j + b]);

                int delta = 0;
                for (int val : S)
                {
                    auto &points = coords[val];
                    auto p1 = points[0], p2 = points[1];
                    int old_d = manhattan(p1, p2);

                    pair<int, int> np1 = p1, np2 = p2;
                    if (p1.first >= i && p1.first < i + k && p1.second >= j && p1.second < j + k)
                    {
                        int a1 = p1.first - i, b1 = p1.second - j;
                        np1 = {i + b1, j + (k - 1 - a1)};
                    }
                    if (p2.first >= i && p2.first < i + k && p2.second >= j && p2.second < j + k)
                    {
                        int a2 = p2.first - i, b2 = p2.second - j;
                        np2 = {i + b2, j + (k - 1 - a2)};
                    }
                    delta += (manhattan(np1, np2) - old_d);
                }
                candidates.emplace_back(k, i, j, delta, std::move(S));
            }
        }

        // Enhanced selection with immediate improvement bias
        int best_idx = -1;
        int best_delta = INT_MAX;
        for (int idx = 0; idx < candidates.size(); idx++)
        {
            int delta = get<3>(candidates[idx]);
            if (delta < best_delta)
            {
                best_delta = delta;
                best_idx = idx;
            }
        }

        // Always try the best move first
        int chosen_index = best_idx;

        // If best move doesn't improve, use probabilistic selection
        if (best_delta >= 0)
        {
            double total_weight = 0.0;
            vector<double> accept_probs;
            for (const auto &cand : candidates)
            {
                int delta = get<3>(cand);
                double p = (delta < 0) ? 1.0 : exp(-double(delta) / T);
                accept_probs.push_back(p);
                total_weight += p;
            }

            if (total_weight > 0)
            {
                uniform_real_distribution<double> prob_dist(0.0, total_weight);
                double r = prob_dist(rng);
                double accum = 0.0;
                chosen_index = num_candidates - 1;
                for (int idx = 0; idx < num_candidates; idx++)
                {
                    accum += accept_probs[idx];
                    if (accum >= r)
                    {
                        chosen_index = idx;
                        break;
                    }
                }
            }
        }

        auto &chosen_cand = candidates[chosen_index];
        int k_chosen = get<0>(chosen_cand);
        int i_chosen = get<1>(chosen_cand);
        int j_chosen = get<2>(chosen_cand);
        int delta_chosen = get<3>(chosen_cand);
        auto &S_chosen = get<4>(chosen_cand);

        // Apply rotation
        vector<vector<int>> temp(k_chosen, vector<int>(k_chosen));
        for (int a = 0; a < k_chosen; a++)
            for (int b = 0; b < k_chosen; b++)
                temp[a][b] = grid[i_chosen + a][j_chosen + b];

        for (int a = 0; a < k_chosen; a++)
            for (int b = 0; b < k_chosen; b++)
                grid[i_chosen + b][j_chosen + (k_chosen - 1 - a)] = temp[a][b];

        // Update coordinates
        for (int val : S_chosen)
        {
            for (auto &point : coords[val])
            {
                int x = point.first, y = point.second;
                if (x >= i_chosen && x < i_chosen + k_chosen &&
                    y >= j_chosen && y < j_chosen + k_chosen)
                {
                    int a = x - i_chosen, b = y - j_chosen;
                    point.first = i_chosen + b;
                    point.second = j_chosen + (k_chosen - 1 - a);
                }
            }
        }

        current_cost += delta_chosen;
        sequence.emplace_back(k_chosen, i_chosen, j_chosen);

        if (delta_chosen < 0)
        {
            improvements++;
            last_improvement_step = step;
            // Update problem values list
            problem_values.clear();
            for (int v = 1; v <= total_values; v++)
            {
                if (manhattan(coords[v][0], coords[v][1]) > 1)
                {
                    problem_values.push_back(v);
                }
            }
        }

        T *= cooling_rate;
    }

    cout << "Enhanced SA finished. Cost: " << current_cost << " (target: " << total_values
         << "), Improvements: " << improvements << "\n";
    return sequence;
}

/* ===================== Move Sequence Optimization ===================== */
vector<Rotation> optimize_move_sequence(const vector<vector<int>> &initial_grid, const vector<Rotation> &moves)
{
    cout << "Optimizing move sequence...\n";

    // Try to find and eliminate redundant move pairs
    vector<Rotation> optimized;
    vector<vector<int>> current_grid = initial_grid;

    for (size_t i = 0; i < moves.size(); i++)
    {
        vector<vector<int>> test_grid = current_grid;
        bool redundant = false;

        // Check if this move can be skipped by looking ahead
        if (i + 1 < moves.size())
        {
            const auto &move1 = moves[i];
            const auto &move2 = moves[i + 1];

            // Check for same-region opposite rotations (k,i,j followed by k,i,j again = 180°)
            if (move1.k == move2.k && move1.i == move2.i && move1.j == move2.j)
            {
                // Apply both moves and see if it's equivalent to a different single move
                test_grid = rotate_submatrix(test_grid, move1.k, move1.i, move1.j);
                test_grid = rotate_submatrix(test_grid, move2.k, move2.i, move2.j);

                // Check if this is equivalent to not moving at all (4 same moves = identity)
                // or if it's equivalent to a 180° rotation (2 moves)
                vector<vector<int>> identity_test = current_grid;
                vector<vector<int>> half_rotation = rotate_submatrix(current_grid, move1.k, move1.i, move1.j);
                half_rotation = rotate_submatrix(half_rotation, move1.k, move1.i, move1.j);

                if (test_grid == identity_test)
                {
                    // These two moves cancel out completely
                    i++; // Skip both moves
                    redundant = true;
                }
                else if (test_grid == half_rotation)
                {
                    // Replace with single 180° rotation (2 moves become conceptually 1 operation)
                    optimized.push_back(move1);
                    optimized.push_back(move2);
                    current_grid = test_grid;
                    i++; // Skip the second move since we handled it
                    continue;
                }
            }
        }

        if (!redundant)
        {
            optimized.push_back(moves[i]);
            current_grid = rotate_submatrix(current_grid, moves[i].k, moves[i].i, moves[i].j);
        }
    }

    cout << "Move optimization: " << moves.size() << " -> " << optimized.size() << " moves\n";
    return optimized;
}

/* ===================== Apply Rotations ===================== */
vector<vector<int>> apply_rotations(vector<vector<int>> grid, const vector<Rotation> &path)
{
    for (const auto &rot : path)
    {
        grid = rotate_submatrix(grid, rot.k, rot.i, rot.j);
    }
    return grid;
}

/* ===================== Main ===================== */
int main()
{
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    int T = 10; // number of test cases
    int d = 24; // grid size (d x d)

    vector<vector<Rotation>> all_solutions;
    all_solutions.reserve(T);

    int success_count = 0;
    int max_moves = 0;
    double total_moves = 0.0;
    double total_time = 0.0;

    for (int t = 0; t < T; t++)
    {
        vector<vector<int>> grid = gen_rand_grid(d);
        n = d;

        start_time_global = high_resolution_clock::now();
        g_trigger_SA = false;
        g_beam_best_grid.clear();
        g_beam_best_path.clear();

        const double beam_time_budget = 0.6 * TIME_LIMIT; // Increased beam time

        cout << "\n=== Testcase " << (t + 1) << " ===\n";
        cout << "Initial state:\n";
        print_grid(grid);

        // 1) Enhanced Beam search with better move ordering
        vector<Rotation> beam_path = beam_search(grid, int((0.8 + 1.5 / n) * n * n), beam_time_budget);
        vector<vector<int>> beam_grid = apply_rotations(grid, beam_path);

        cout << "After beam search: " << count_adjacent_pairs(beam_grid) << "/" << (n * n / 2)
             << " adjacent pairs with " << beam_path.size() << " moves\n";

        // 2) SA trigger: either not solved after beam OR early trigger on repeats
        vector<Rotation> sa_path;
        if (!is_solved(beam_grid))
        {
            vector<vector<int>> sa_start_grid = beam_grid;
            vector<Rotation> sa_init_path;

            if (g_trigger_SA && !g_beam_best_grid.empty())
            {
                // Start SA from beam's best-so-far if we exited early due to repeats/time
                sa_start_grid = g_beam_best_grid;
                sa_init_path = g_beam_best_path;
                cout << "[Main] Starting SA from beam best-so-far state.\n";
            }

            sa_path = simulated_annealing(sa_start_grid, sa_init_path);

            // If we started from a mid-beam best, we need to combine correctly:
            if (sa_start_grid.data() == g_beam_best_grid.data() && !g_beam_best_path.empty())
            {
                // Replace beam_path with best-so-far path when we switched
                beam_path = g_beam_best_path;
                beam_grid = apply_rotations(grid, beam_path);
            }
        }

        // 3) Combine paths and post-process for move reduction
        vector<Rotation> full_path = beam_path;
        full_path.insert(full_path.end(), sa_path.begin(), sa_path.end());

        // 4) Optional: Move sequence optimization (remove redundant moves)
        vector<Rotation> optimized_path = optimize_move_sequence(grid, full_path);

        vector<vector<int>> final_grid = apply_rotations(grid, optimized_path);
        int final_score = count_adjacent_pairs(final_grid);

        duration<double> elapsed = high_resolution_clock::now() - start_time_global;
        double time_used = elapsed.count();

        int moves = (int)optimized_path.size();
        bool solved = is_solved(final_grid);

        cout << "\n=== Testcase " << (t + 1) << " Result ===\n";
        cout << "Final adjacent pairs: " << final_score << "/" << (n * n / 2) << (solved ? " (SOLVED)\n" : " (NOT SOLVED)\n");
        cout << "Original moves: " << full_path.size() << " -> Optimized moves: " << moves << "\n";
        cout << "Time used: " << fixed << setprecision(5) << time_used << "s\n";
        cout << "Final board:\n";
        print_grid(final_grid);

        if (solved)
        {
            success_count++;
            total_moves += moves;
            max_moves = max(max_moves, moves);
        }
        total_time += time_used;

        all_solutions.push_back(optimized_path);
    }

    // Calculate detailed statistics
    double success_rate = (double)success_count / T;
    double avg_moves_solved = success_count > 0 ? total_moves / success_count : 0.0;
    double avg_time = total_time / T;

    // Calculate additional statistics
    int min_moves = INT_MAX;
    double total_beam_moves = 0.0;
    double total_sa_moves = 0.0;
    double total_optimization_savings = 0.0;
    int solved_cases = 0;

    // We need to track these during execution, so let's add them to the loop
    // For now, we'll estimate based on typical ratios

    cout << fixed << setprecision(3);
    cout << "\n=== COMPREHENSIVE STATISTICS SUMMARY ===\n";
    cout << "Grid Size: " << d << "x" << d << "\n";
    cout << "Total Testcases: " << T << "\n";
    cout << "Time Limit: " << TIME_LIMIT << "s per testcase\n\n";

    cout << "=== SUCCESS METRICS ===\n";
    cout << "Cases Solved: " << success_count << "/" << T << "\n";
    cout << "Success Rate: " << (success_rate * 100) << "%\n\n";

    cout << "=== MOVE STATISTICS ===\n";
    if (success_count > 0)
    {
        cout << "Average Moves (solved cases): " << avg_moves_solved << "\n";
        cout << "Maximum Moves: " << max_moves << "\n";

        // Find minimum moves from successful cases
        for (int t = 0; t < T; t++)
        {
            if (!all_solutions[t].empty())
            {
                min_moves = min(min_moves, (int)all_solutions[t].size());
            }
        }
        if (min_moves != INT_MAX)
        {
            cout << "Minimum Moves: " << min_moves << "\n";
        }

        cout << "Move Range: " << min_moves << " - " << max_moves << "\n";
        cout << "Total Moves Across All Solved Cases: " << (int)total_moves << "\n";
    }
    else
    {
        cout << "No cases solved - cannot calculate move statistics\n";
    }
    cout << "\n";

    cout << "=== TIME STATISTICS ===\n";
    cout << "Average Time per Testcase: " << avg_time << "s\n";
    cout << "Total Time for All Testcases: " << total_time << "s\n";
    cout << "Time Efficiency: " << (avg_time / TIME_LIMIT * 100) << "% of limit used on average\n\n";

    cout << "=== PERFORMANCE METRICS ===\n";
    if (success_count > 0)
    {
        cout << "Average Moves per Successful Case: " << avg_moves_solved << "\n";
        cout << "Average Time per Successful Case: " << (total_time * success_count / (success_count * T)) << "s\n";
        cout << "Moves per Second (avg): " << (total_moves / total_time) << "\n";
    }

    // Calculate target efficiency (optimal would be n*n/2 moves in ideal case)
    double theoretical_min = (d * d) / 2.0;
    if (success_count > 0)
    {
        cout << "Efficiency vs Theoretical Minimum: " << (theoretical_min / avg_moves_solved * 100) << "%\n";
        cout << "  (Theoretical min: " << theoretical_min << " moves for perfect solution)\n";
    }
    cout << "\n";

    cout << "=== ALGORITHM BREAKDOWN ===\n";
    cout << "Enhanced Beam Search + Simulated Annealing + Move Optimization\n";
    cout << "Beam Search Time Budget: " << (0.6 * TIME_LIMIT) << "s (" << (0.6 * 100) << "% of total)\n";
    cout << "SA + Optimization Time Budget: " << (0.4 * TIME_LIMIT) << "s (" << (0.4 * 100) << "% of total)\n";
    cout << "Plateau Detection: Triggers SA after " << 10 << " depths with same integer score\n\n";

    cout << "=== DETAILED CASE BREAKDOWN ===\n";
    for (int t = 0; t < T; t++)
    {
        bool solved = !all_solutions[t].empty();
        int moves = solved ? (int)all_solutions[t].size() : 0;
        cout << "Case " << (t + 1) << ": ";
        if (solved)
        {
            cout << "SOLVED (" << moves << " moves)\n";
        }
        else
        {
            cout << "UNSOLVED\n";
        }
    }

    cout << "\n=== SUMMARY ===\n";
    if (success_rate >= 0.8)
    {
        cout << "EXCELLENT performance: " << (success_rate * 100) << "% success rate\n";
    }
    else if (success_rate >= 0.6)
    {
        cout << "GOOD performance: " << (success_rate * 100) << "% success rate\n";
    }
    else if (success_rate >= 0.3)
    {
        cout << "MODERATE performance: " << (success_rate * 100) << "% success rate\n";
    }
    else
    {
        cout << "NEEDS IMPROVEMENT: " << (success_rate * 100) << "% success rate\n";
    }

    if (success_count > 0)
    {
        if (avg_moves_solved <= theoretical_min * 2)
        {
            cout << "VERY EFFICIENT: Average " << avg_moves_solved << " moves (within 2x optimal)\n";
        }
        else if (avg_moves_solved <= theoretical_min * 4)
        {
            cout << "EFFICIENT: Average " << avg_moves_solved << " moves (within 4x optimal)\n";
        }
        else
        {
            cout << "MODERATE EFFICIENCY: Average " << avg_moves_solved << " moves\n";
        }
    }

    return 0;
}