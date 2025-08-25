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
    int score; // Number of adjacent pairs (higher is better)
    bool operator<(const GridState &other) const
    {
        return score < other.score; // For max-heap (higher score is better)
    }
};

int n;
auto start_time_global = high_resolution_clock::now();
const double TIME_LIMIT = 270.0; // 4.5 minutes

// Early-SA trigger state (set inside beam search)
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

/* ===================== Beam Search ===================== */
vector<Rotation> beam_search(const vector<vector<int>> &initial_grid, int max_depth, double time_budget)
{
    auto beam_start = high_resolution_clock::now();
    int initial_score = count_adjacent_pairs(initial_grid);
    int target_score = n * n / 2;

    // Dynamic beam width based on grid size
    int beam_width = max(1, 5760 / (n * n));
    cout << "Starting Beam Search (beam width: " << beam_width << ")\n";
    cout << "Initial adjacent pairs: " << initial_score << "/" << target_score << '\n';

    priority_queue<GridState> beam;
    beam.push({initial_grid, {}, initial_score});

    GridState global_best = {initial_grid, {}, initial_score};
    int depth = 0;

    // For "best_score_in_depth repeating N times" reporting and early SA trigger
    int prev_best_score_in_depth = -1;
    int same_best_depth_streak = 0;

    for (; depth < max_depth && !beam.empty(); ++depth)
    {
        priority_queue<GridState> next_beam;
        unordered_set<string> visited;
        int states_processed = 0;
        int best_score_in_depth = -1;

        while (!beam.empty())
        {
            // Check time budget
            auto now_time = high_resolution_clock::now();
            double elapsed = duration<double>(now_time - beam_start).count();
            if (elapsed > time_budget || is_time_up())
            {
                cout << "Beam search time budget exceeded at depth " << depth << '\n';
                // Save best-so-far for SA
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
                cout << "Solution found at depth " << depth << '\n';
                return current.path;
            }

            for (int k = 2; k <= min(8, n - 1); ++k)
            {
                for (int i = 0; i <= n - k; ++i)
                {
                    for (int j = 0; j <= n - k; ++j)
                    {
                        vector<vector<int>> new_grid = rotate_submatrix(current.grid, k, i, j);
                        int new_score = count_adjacent_pairs(new_grid);
                        vector<Rotation> new_path = current.path;
                        new_path.emplace_back(k, i, j);

                        if (new_score > best_score_in_depth)
                        {
                            best_score_in_depth = new_score;
                        }
                        if (new_score == target_score)
                        {
                            cout << "Solution found at depth " << depth << '\n';
                            return new_path;
                        }

                        string grid_key = get_grid_key(new_grid);
                        if (visited.find(grid_key) == visited.end())
                        {
                            visited.insert(grid_key);
                            next_beam.push({std::move(new_grid), std::move(new_path), new_score});
                        }
                    }
                }
            }
        }

        cout << "Depth " << depth << ": processed " << states_processed
             << " states, best score: " << best_score_in_depth << "/" << target_score << '\n';

        // ======= Repeats reporting (consecutive depths with same best) =======
        if (best_score_in_depth == prev_best_score_in_depth)
        {
            same_best_depth_streak++;
            // Print "X/Y t times" exactly as requested
            cout << best_score_in_depth << "/" << target_score << " " << same_best_depth_streak << " times\n";

            // Trigger SA early if repeated 10 consecutive depths
            if (same_best_depth_streak >= 10)
            {
                cout << "[Beam] Best score repeated " << same_best_depth_streak
                     << " times — triggering Simulated Annealing early.\n";
                g_trigger_SA = true;
                g_beam_best_grid = global_best.grid;
                g_beam_best_path = global_best.path;
                return global_best.path; // return best path so far
            }
        }
        else
        {
            same_best_depth_streak = 0;
        }
        prev_best_score_in_depth = best_score_in_depth;

        // Keep top states for next depth (score-based pruning)
        beam = priority_queue<GridState>();
        int kept = 0;
        while (!next_beam.empty() && kept < beam_width)
        {
            GridState state = next_beam.top();
            // Keep near-best in this depth (slightly looser for small n)
            if (state.score >= best_score_in_depth - (n > 4 ? 2 : 1))
            {
                beam.push(state);
                kept++;
            }
            next_beam.pop();
        }
        cout << "Kept " << kept << " states for next depth\n";
    }
    cout << "Beam search completed to depth " << depth << '\n';

    // Save best for SA if not solved
    g_trigger_SA = true;
    g_beam_best_grid = global_best.grid;
    g_beam_best_path = global_best.path;
    return global_best.path;
}

/* ===================== Simulated Annealing ===================== */
vector<Rotation> simulated_annealing(vector<vector<int>> grid, const vector<Rotation> & /*initial_path*/)
{
    int total_values = (n * n) / 2;

    cout << "\nStarting Simulated Annealing\n";

    vector<vector<pair<int, int>>> coords(total_values + 1);
    coords.reserve(total_values + 1);
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

    const int max_steps = 40000;
    const int num_candidates = 300;
    double T = 5.0 * n;
    const double cooling_rate = 0.95;

    vector<Rotation> sequence;
    unsigned long long seed = (unsigned long long)chrono::steady_clock::now().time_since_epoch().count();
    xoshiro256ss rng(seed);

    vector<double> weights_k;
    weights_k.reserve(n - 1);
    for (int k = 2; k <= n; k++)
        weights_k.push_back(1.0 / (k * k));
    discrete_distribution<int> k_distr(weights_k.begin(), weights_k.end());

    auto start_time = chrono::steady_clock::now();

    for (int step = 0; step < max_steps; step++)
    {
        if (current_cost == total_values)
            break;

        auto now = chrono::steady_clock::now();
        auto elapsed = chrono::duration_cast<chrono::seconds>(now - start_time).count();
        if (elapsed >= 270 || is_time_up())
            break;

        vector<tuple<int, int, int, int, unordered_set<int>>> candidates;
        candidates.reserve(num_candidates);

        for (int c = 0; c < num_candidates; c++)
        {
            int k = 2 + k_distr(rng);
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
                auto p1 = points[0];
                auto p2 = points[1];
                int old_d = manhattan(p1, p2);

                // rotate positions if inside submatrix
                pair<int, int> np1 = p1;
                if (p1.first >= i && p1.first < i + k && p1.second >= j && p1.second < j + k)
                {
                    int a1 = p1.first - i, b1 = p1.second - j;
                    np1 = {i + b1, j + (k - 1 - a1)};
                }
                pair<int, int> np2 = p2;
                if (p2.first >= i && p2.first < i + k && p2.second >= j && p2.second < j + k)
                {
                    int a2 = p2.first - i, b2 = p2.second - j;
                    np2 = {i + b2, j + (k - 1 - a2)};
                }

                delta += (manhattan(np1, np2) - old_d);
            }

            candidates.emplace_back(k, i, j, delta, std::move(S));
        }

        double total_weight = 0.0;
        vector<double> accept_probs;
        accept_probs.reserve(num_candidates);
        for (const auto &cand : candidates)
        {
            int delta = get<3>(cand);
            double p = (delta < 0) ? 1.0 : exp(-double(delta) / T);
            accept_probs.push_back(p);
            total_weight += p;
        }

        uniform_real_distribution<double> prob_dist(0.0, total_weight);
        double r = prob_dist(rng);
        double accum = 0.0;
        int chosen_index = num_candidates - 1;
        for (int idx = 0; idx < num_candidates; idx++)
        {
            accum += accept_probs[idx];
            if (accum >= r)
            {
                chosen_index = idx;
                break;
            }
        }

        auto &chosen_cand = candidates[chosen_index];
        int k_chosen = get<0>(chosen_cand);
        int i_chosen = get<1>(chosen_cand);
        int j_chosen = get<2>(chosen_cand);
        int delta_chosen = get<3>(chosen_cand);
        auto &S_chosen = get<4>(chosen_cand);

        // Apply rotation to grid
        vector<vector<int>> temp(k_chosen, vector<int>(k_chosen));
        for (int a = 0; a < k_chosen; a++)
            for (int b = 0; b < k_chosen; b++)
                temp[a][b] = grid[i_chosen + a][j_chosen + b];

        for (int a = 0; a < k_chosen; a++)
            for (int b = 0; b < k_chosen; b++)
                grid[i_chosen + b][j_chosen + (k_chosen - 1 - a)] = temp[a][b];

        // Update coords for affected values
        for (int val : S_chosen)
        {
            for (auto &point : coords[val])
            {
                int x = point.first;
                int y = point.second;
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
        T *= cooling_rate;
    }

    cout << "SA finished. Cost: " << current_cost << " (target: " << (n * n) / 2 << ")\n";
    return sequence;
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

    int T = 3;  // number of test cases
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

        const double beam_time_budget = 0.5 * TIME_LIMIT;

        // 1) Beam search
        vector<Rotation> beam_path = beam_search(grid, int((0.6 + 2.0 / n) * n * n), beam_time_budget);
        vector<vector<int>> beam_grid = apply_rotations(grid, beam_path);

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

        // 3) Combine paths and finalize
        vector<Rotation> full_path = beam_path;
        full_path.insert(full_path.end(), sa_path.begin(), sa_path.end());

        vector<vector<int>> final_grid = apply_rotations(grid, full_path);
        int final_score = count_adjacent_pairs(final_grid);

        duration<double> elapsed = high_resolution_clock::now() - start_time_global;
        double time_used = elapsed.count();

        int moves = (int)full_path.size();
        bool solved = is_solved(final_grid);

        cout << "\n=== Testcase " << (t + 1) << " Result ===\n";
        cout << "Final adjacent pairs: " << final_score << "/" << (n * n / 2) << (solved ? " (SOLVED)\n" : " (NOT SOLVED)\n");
        cout << "Total moves: " << moves << "\n";
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

        all_solutions.push_back(full_path);
    }

    cout << fixed << setprecision(5);
    cout << "\n=== Final Summary ===\n";
    cout << d << "x" << d << " " << T << " testcase\n";
    cout << "Success rate : " << (double)success_count / T << "\n";
    if (success_count > 0)
    {
        cout << "Avg Moves : " << total_moves / success_count << "\n";
    }
    else
    {
        cout << "Avg Moves : N/A\n";
    }
    cout << "Avg time : " << total_time / T << "\n";
    cout << "Max move : " << max_moves << "\n";

    return 0;
}
