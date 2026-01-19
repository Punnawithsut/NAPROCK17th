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
#include <thread> // for sleep_for

using namespace std;
using namespace std::chrono;

vector<vector<int>> gen_rand_grid(int n)
{
    srand(time(0) ^ rand());
    int assignedValues = n * n / 2;
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

class xoshiro256ss
{
public:
    using result_type = uint64_t;
    xoshiro256ss(uint64_t seed = 0)
    {
        s[0] = splitmix64(seed);
        s[1] = splitmix64(s[0]);
        s[2] = splitmix64(s[1]);
        s[3] = splitmix64(s[2]);
    }

    static constexpr uint64_t min() { return 0; }
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
    static inline uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t splitmix64(uint64_t x)
    {
        uint64_t z = (x += 0x9e3779b97f4a7c15);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
        z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
        return z ^ (z >> 31);
    }
    uint64_t s[4];
};

struct Rotation
{
    int k, i, j;
    Rotation(int k, int i, int j) : k(k), i(i), j(j) {}
};
struct GridState
{
    vector<vector<int>> grid;
    vector<Rotation> path;
    int score;
    bool operator<(const GridState &other) const { return score < other.score; }
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

void print_grid(const vector<vector<int>> &grid)
{
    for (const auto &row : grid)
    {
        for (int val : row)
            cout << setw(2) << val << " ";
        cout << endl;
    }
}

int count_adjacent_pairs(const vector<vector<int>> &grid)
{
    int count = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
        {
            if (i + 1 < n && grid[i][j] == grid[i + 1][j])
                count++;
            if (j + 1 < n && grid[i][j] == grid[i][j + 1])
                count++;
        }
    return count;
}

bool is_solved(const vector<vector<int>> &grid) { return count_adjacent_pairs(grid) == n * n / 2; }

vector<vector<int>> rotate_submatrix(const vector<vector<int>> &grid, int k, int i, int j)
{
    vector<vector<int>> new_grid = grid;
    for (int x = 0; x < k; ++x)
        for (int y = 0; y < k; ++y)
            new_grid[i + y][j + k - 1 - x] = grid[i + x][j + y];
    return new_grid;
}

string get_grid_key(const vector<vector<int>> &grid)
{
    string key;
    for (const auto &row : grid)
        for (int val : row)
            key += to_string(val) + ',';
    return key;
}

vector<Rotation> beam_search(const vector<vector<int>> &initial_grid, int max_depth, double time_budget)
{
    auto beam_start = high_resolution_clock::now();
    int initial_score = count_adjacent_pairs(initial_grid);
    int target_score = n * n / 2;
    int beam_width = 5760 / (n * n);

    priority_queue<GridState> beam;
    beam.push({initial_grid, {}, initial_score});
    GridState global_best = {initial_grid, {}, initial_score};

    for (int depth = 0; depth < max_depth && !beam.empty(); ++depth)
    {
        priority_queue<GridState> next_beam;
        unordered_set<string> visited;
        int best_score_in_depth = -1;

        while (!beam.empty())
        {
            auto now_time = high_resolution_clock::now();
            if (duration<double>(now_time - beam_start).count() > time_budget)
                return global_best.path;

            GridState current = beam.top();
            beam.pop();
            if (current.score > global_best.score)
                global_best = current;
            if (is_solved(current.grid))
                return current.path;

            for (int k = 2; k <= min(8, n - 1); ++k)
                for (int i = 0; i <= n - k; ++i)
                    for (int j = 0; j <= n - k; ++j)
                    {
                        vector<vector<int>> new_grid = rotate_submatrix(current.grid, k, i, j);
                        int new_score = count_adjacent_pairs(new_grid);
                        vector<Rotation> new_path = current.path;
                        new_path.emplace_back(k, i, j);

                        if (new_score > best_score_in_depth)
                            best_score_in_depth = new_score;
                        string key = get_grid_key(new_grid);
                        if (visited.find(key) == visited.end())
                        {
                            visited.insert(key);
                            next_beam.push({new_grid, new_path, new_score});
                        }
                    }
        }

        beam = priority_queue<GridState>();
        int kept = 0;
        while (!next_beam.empty() && kept < beam_width)
        {
            GridState state = next_beam.top();
            next_beam.pop();
            if (state.score >= best_score_in_depth - (n > 4 ? 2 : 1))
            {
                beam.push(state);
                kept++;
            }
        }
    }
    return global_best.path;
}

vector<Rotation> simulated_annealing(vector<vector<int>> grid)
{
    int total_values = n * n / 2;
    vector<vector<pair<int, int>>> coords(total_values + 1);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            coords[grid[i][j]].push_back({i, j});

    int current_cost = 0;
    for (int v = 1; v <= total_values; v++)
        current_cost += abs(coords[v][0].first - coords[v][1].first) + abs(coords[v][0].second - coords[v][1].second);

    int max_steps = 40000, num_candidates = 300;
    double T = 5.0 * n, cooling_rate = 0.95;
    vector<Rotation> sequence;
    xoshiro256ss rng(chrono::steady_clock::now().time_since_epoch().count());
    vector<double> weights_k;
    for (int k = 2; k <= n; k++)
        weights_k.push_back(1.0 / (k * k));
    discrete_distribution<int> k_distr(weights_k.begin(), weights_k.end());
    auto start_time = chrono::steady_clock::now();
    auto last_update = start_time;

    for (int step = 0; step < max_steps; step++)
    {
        if (current_cost == total_values)
            break;
        auto now = chrono::steady_clock::now();
        if (chrono::duration_cast<chrono::seconds>(now - last_update).count() >= 1)
        {
            int current_pairs = total_values - current_cost;
            cout << "\rCurrent paired numbers: " << current_pairs << "/" << total_values << flush;
            last_update = now;
        }

        vector<tuple<int, int, int, int, unordered_set<int>>> candidates;
        candidates.reserve(num_candidates);
        for (int c = 0; c < num_candidates; c++)
        {
            int k = 2 + k_distr(rng);
            uniform_int_distribution<int> i_dist(0, n - k), j_dist(0, n - k);
            int i = i_dist(rng), j = j_dist(rng);
            unordered_set<int> S;
            for (int a = 0; a < k; a++)
                for (int b = 0; b < k; b++)
                    S.insert(grid[i + a][j + b]);
            int delta = 0;
            for (int val : S)
            {
                auto &p = coords[val];
                auto p1 = p[0], p2 = p[1];
                int old_d = abs(p1.first - p2.first) + abs(p1.second - p2.second);
                pair<int, int> new_p1 = p1, new_p2 = p2;
                if (p1.first >= i && p1.first < i + k && p1.second >= j && p1.second < j + k)
                {
                    int a1 = p1.first - i, b1 = p1.second - j;
                    new_p1 = {i + b1, j + (k - 1 - a1)};
                }
                if (p2.first >= i && p2.first < i + k && p2.second >= j && p2.second < j + k)
                {
                    int a2 = p2.first - i, b2 = p2.second - j;
                    new_p2 = {i + b2, j + (k - 1 - a2)};
                }
                delta += abs(new_p1.first - new_p2.first) + abs(new_p1.second - new_p2.second) - old_d;
            }
            candidates.emplace_back(k, i, j, delta, S);
        }

        double total_weight = 0;
        vector<double> accept_probs;
        accept_probs.reserve(num_candidates);
        for (auto &cand : candidates)
        {
            int delta = get<3>(cand);
            accept_probs.push_back(delta < 0 ? 1.0 : exp(-delta / T));
            total_weight += accept_probs.back();
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
        int k_chosen = get<0>(chosen_cand), i_chosen = get<1>(chosen_cand), j_chosen = get<2>(chosen_cand), delta_chosen = get<3>(chosen_cand);
        auto S_chosen = get<4>(chosen_cand);
        vector<vector<int>> temp(k_chosen, vector<int>(k_chosen));
        for (int a = 0; a < k_chosen; a++)
            for (int b = 0; b < k_chosen; b++)
                temp[a][b] = grid[i_chosen + a][j_chosen + b];
        for (int a = 0; a < k_chosen; a++)
            for (int b = 0; b < k_chosen; b++)
                grid[i_chosen + b][j_chosen + (k_chosen - 1 - a)] = temp[a][b];
        for (int val : S_chosen)
            for (auto &point : coords[val])
            {
                int x = point.first, y = point.second;
                if (x >= i_chosen && x < i_chosen + k_chosen && y >= j_chosen && y < j_chosen + k_chosen)
                {
                    int a = x - i_chosen, b = y - j_chosen;
                    point.first = i_chosen + b;
                    point.second = j_chosen + (k_chosen - 1 - a);
                }
            }
        current_cost += delta_chosen;
        sequence.emplace_back(k_chosen, i_chosen, j_chosen);
        T *= cooling_rate;
    }

    cout << "\nSA finished. Cost: " << current_cost << "/" << total_values << "\n";
    return sequence;
}

vector<vector<int>> apply_rotations(vector<vector<int>> grid, const vector<Rotation> &path)
{
    for (auto &rot : path)
        grid = rotate_submatrix(grid, rot.k, rot.i, rot.j);
    return grid;
}

int main()
{
    int T = 1;
    int d = 24;
    int success_count = 0;
    double total_moves = 0.0, total_time = 0.0;
    int max_moves = 0;
    vector<vector<Rotation>> all_solutions;

    for (int t = 0; t < T; t++)
    {
        vector<vector<int>> grid = gen_rand_grid(d);
        n = d;
        start_time = high_resolution_clock::now();
        double beam_time_budget = 0.5 * TIME_LIMIT;

        vector<Rotation> beam_path = beam_search(grid, int((0.6 + 2.0 / n) * n * n), beam_time_budget);
        vector<vector<int>> beam_grid = apply_rotations(grid, beam_path);

        vector<Rotation> sa_path;
        if (!is_solved(beam_grid))
            sa_path = simulated_annealing(beam_grid);

        vector<Rotation> full_path = beam_path;
        full_path.insert(full_path.end(), sa_path.begin(), sa_path.end());
        vector<vector<int>> final_grid = apply_rotations(grid, full_path);
        int final_score = count_adjacent_pairs(final_grid);

        cout << "\nFinal board (" << final_score << " adjacent pairs):\n";
        print_grid(final_grid);

        duration<double> elapsed = high_resolution_clock::now() - start_time;
        double time_used = elapsed.count();
        int moves = full_path.size();
        if (is_solved(final_grid))
        {
            success_count++;
            total_moves += moves;
            max_moves = max(max_moves, moves);
        }
        total_time += time_used;
        all_solutions.push_back(full_path);
    }

    cout << "\n=== Final Summary ===\n";
    cout << d << "x" << d << " " << T << " testcase\n";
    cout << "Success rate : " << (double)success_count / T << "\n";
    if (success_count > 0)
        cout << "Avg Moves : " << total_moves / success_count << "\n";
    else
        cout << "Avg Moves : N/A\n";
    cout << "Avg time : " << total_time / T << "\n";
    cout << "Max move : " << max_moves << "\n";
    return 0;
}