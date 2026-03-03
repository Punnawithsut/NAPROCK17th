#include <bits/stdc++.h>
#include <fstream>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <omp.h>
#include <random>

using namespace std;
using namespace std::chrono;
using json = nlohmann::json;

// ========== NEW: Configurable backtrack depth ==========
const int BACKTRACK_DEPTH = 10;      // go back this many snapshots when stuck
const int STUCK_LIMIT = 6;           // trigger backtrack after this many non‑improving depths
// ========================================================

struct Rotation
{
    int k, i, j;
    Rotation(int k, int i, int j) : k(k), i(i), j(j) {}
};

struct ExtendedPair
{
    tuple<int, int, int, int> position; // top-left row, top-left col, bottom-right row, bottom-right col
    int cost;
    vector<Rotation> path;
};

vector<vector<uint8_t>> locked;
int n;
int cnt = 0;
int side = 0;
bool broke = false;
bool SET_FP = true;
bool DEBUG = false;
bool ext_pair = false;

struct GridState
{
    vector<vector<uint16_t>> grid;
    vector<Rotation> path;
    int paired_count;
    int heuristic;

    bool operator<(const GridState &other) const
    {
        if (paired_count != other.paired_count)
            return paired_count < other.paired_count;
        return heuristic > other.heuristic;
    }
};

struct StateSnapshot
{
    vector<GridState> beam_states;
    int paired_count;
    int depth_at_snapshot;
    double beam_multiplier;
};

auto start_time = high_resolution_clock::now();
const double TIME_LIMIT = 270.0;

bool is_time_up()
{
    auto now = high_resolution_clock::now();
    duration<double> elapsed = now - start_time;
    return elapsed.count() >= TIME_LIMIT;
}

vector<vector<uint16_t>> get_random_board(int n)
{
    json requestBody = {
        {"boardSize", n}};

    cpr::Response r = cpr::Post(
        cpr::Url{"https://naprock-server.vercel.app/getBoard"},
        cpr::Body{requestBody.dump()},
        cpr::Header{{"Content-Type", "application/json"}});

    json responseData = json::parse(r.text);
    vector<vector<uint16_t>> board(n, vector<uint16_t>(n));

    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            board[i][j] = static_cast<uint16_t>(responseData["board"][i][j].get<int>());

    return board;
}

void save_file(vector<vector<uint16_t>> &og, vector<Rotation> &fp)
{
    const string SAVE_PATH = "result.json";
    json result_file;

    json board_json = json::array();
    for (const auto &row : og)
    {
        json row_json = json::array();
        for (uint16_t val : row)
            row_json.push_back(static_cast<int>(val));
        board_json.push_back(row_json);
    }

    result_file["initialBoard"] = board_json;
    json rotations_map = json::object();

    for (int k = 0; k < (int)fp.size(); k++)
    {
        string rotation_key = to_string(k + 1);
        rotations_map[rotation_key] = {
            {"k", fp[k].k},
            {"i", fp[k].i},
            {"j", fp[k].j}};
    }

    result_file["rotation"] = rotations_map;
    ofstream file(SAVE_PATH);
    if (file.is_open())
    {
        file << result_file.dump(4);
        file.close();
        cout << "Saved successfully" << endl;
    }
    else
    {
        cout << "Couldn't open file, save file failed" << endl;
    }
}

vector<vector<uint16_t>> rotate_submatrix(vector<vector<uint16_t>> grid, int k, int i, int j)
{
    vector<vector<uint16_t>> temp(k, vector<uint16_t>(k));

    for (int x = 0; x < k; ++x)
        for (int y = 0; y < k; ++y)
            temp[y][k - 1 - x] = grid[i + x][j + y];

    for (int x = 0; x < k; ++x)
        for (int y = 0; y < k; ++y)
            grid[i + x][j + y] = temp[x][y];
    return grid;
}

vector<vector<uint8_t>> rotate_submatrix_u8(vector<vector<uint8_t>> grid, int k, int i, int j)
{
    vector<vector<uint8_t>> temp(k, vector<uint8_t>(k));

    for (int x = 0; x < k; ++x)
        for (int y = 0; y < k; ++y)
            temp[y][k - 1 - x] = grid[i + x][j + y];

    for (int x = 0; x < k; ++x)
        for (int y = 0; y < k; ++y)
            grid[i + x][j + y] = temp[x][y];
    return grid;
}

vector<vector<uint16_t>> apply_rotations(vector<vector<uint16_t>> grid, const vector<Rotation> &path)
{
    for (const auto &rot : path)
        grid = rotate_submatrix(grid, rot.k, rot.i, rot.j);
    return grid;
}

void print_grid(const vector<vector<uint16_t>> &grid)
{
    for (const auto &row : grid)
    {
        for (uint16_t val : row)
            cout << setw(3) << val << " ";
        cout << endl;
    }
}

void print_grid(const vector<vector<uint8_t>> &grid)
{
    for (const auto &row : grid)
    {
        for (uint8_t val : row)
            cout << setw(3) << static_cast<int>(val) << " ";
        cout << endl;
    }
}

string serialize(const vector<vector<uint16_t>> &g)
{
    string s;
    int N = g.size();
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            s += to_string(g[r][c]) + ",";
    return s;
}

string get_grid_key(const vector<vector<uint16_t>> &grid)
{
    return serialize(grid);
}

int count_adjacent_pairs(const vector<vector<uint16_t>> &grid)
{
    int rows = grid.size();
    int cols = grid.empty() ? 0 : grid[0].size();
    int count = 0;
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j)
        {
            if (i + 1 < rows && grid[i][j] == grid[i + 1][j])
                count++;
            if (j + 1 < cols && grid[i][j] == grid[i][j + 1])
                count++;
        }
    return count;
}

int count_paired_values(const vector<vector<uint16_t>> &grid)
{
    int rows = grid.size();
    int cols = grid.empty() ? 0 : grid[0].size();
    int max_val = 0;
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            max_val = max(max_val, (int)grid[i][j]);

    vector<vector<pair<int, int>>> coords(max_val + 1);
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            coords[grid[i][j]].push_back({i, j});

    int paired_count = 0;
    for (int v = 0; v <= max_val; v++)
    {
        if (coords[v].size() < 2)
            continue;
        auto &p1 = coords[v][0];
        auto &p2 = coords[v][1];
        int d = abs(p1.first - p2.first) + abs(p1.second - p2.second);
        if (d == 1)
            paired_count++;
    }
    return paired_count;
}

int calculate_manhattan_heuristic(const vector<vector<uint16_t>> &grid)
{
    int rows = grid.size();
    int cols = grid.empty() ? 0 : grid[0].size();
    int max_val = 0;
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            max_val = max(max_val, (int)grid[i][j]);

    vector<vector<pair<int, int>>> coords(max_val + 1);
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            coords[grid[i][j]].push_back({i, j});

    int total_distance = 0;
    for (int v = 0; v <= max_val; v++)
    {
        if (coords[v].size() < 2)
            continue;
        auto &p1 = coords[v][0];
        auto &p2 = coords[v][1];
        int d = abs(p1.first - p2.first) + abs(p1.second - p2.second) +
                abs(max(p1.first - p2.first, p1.second - p2.second));
        total_distance += d;
    }
    return total_distance;
}

bool is_solved(const vector<vector<uint16_t>> &grid)
{
    int rows = grid.size();
    int cols = grid.empty() ? 0 : grid[0].size();
    // Number of distinct values = rows*cols/2 (each value appears exactly twice)
    return count_paired_values(grid) == (rows * cols) / 2;
}

bool Check_Valid(int i, int j, int k)
{
    if (locked[i][j] || locked[i + k][j + k] || locked[i][j + k] || locked[i + k][j])
        return false;
    for (int ft = 1; ft < k; ft += 1)
        if (locked[i + ft][j] || locked[i][j + ft] || locked[i + ft][j + k] || locked[i + k][j + ft])
            return false;
    return true;
}

struct State
{
    vector<vector<uint16_t>> grid;
    vector<Rotation> path;
    pair<int, int> pos;
};

pair<int, int> find_pos(const vector<vector<uint16_t>> &grid, int row, int col)
{
    if (grid.empty() || row < 0 || col < 0 || row >= (int)grid.size() || col >= (int)grid[0].size())
        return {-1, -1};

    uint16_t target = grid[row][col];
    for (int i = 0; i < (int)grid.size(); ++i)
        for (int j = 0; j < (int)grid[i].size(); ++j)
        {
            if (i == row && j == col)
                continue;
            if (grid[i][j] == target)
                return {i, j};
        }
    return {-1, -1};
}

vector<tuple<int, int, int, int, uint16_t>> find_free_pairs(const vector<vector<uint16_t>> &grid, int track)
{
    vector<tuple<int, int, int, int, uint16_t>> pairs;
    int N = grid.size();

    for (int j = track; j < N; ++j)
    {
        for (int i = N / 2 - 3; i < N; ++i)
        {
            if (locked[cnt + i][cnt + j])
                continue;
            if (j + 1 < N && !locked[cnt + i][cnt + j + 1] && grid[i][j] == grid[i][j + 1])
                pairs.push_back({i, j, i, j + 1, grid[i][j]});
            if (i + 1 < N && !locked[cnt + i + 1][cnt + j] && grid[i][j] == grid[i + 1][j])
                pairs.push_back({i, j, i + 1, j, grid[i][j]});
        }
    }
    return pairs;
}

pair<int, vector<Rotation>> move_free_pair_to_target(const vector<vector<uint16_t>> &initial_grid,
                                                     int pr1, int pc1, int pr2, int pc2,
                                                     int target_r, int target_c, bool is_vertical)
{
    const int beam_width = 20;
    const int max_depth = 2;
    int N = initial_grid.size();
    vector<State> current_beam = {{initial_grid, {}, {pr1, pc1}}};

    for (int depth = 0; depth < max_depth; ++depth)
    {
        vector<State> next_beam;

        for (const auto &cur : current_beam)
        {
            bool at_target = false;
            if (is_vertical)
                at_target = (cur.grid[target_r][target_c] == cur.grid[target_r + 1][target_c] &&
                             cur.grid[target_r][target_c] == initial_grid[pr1][pc1]);
            else
                at_target = (cur.grid[target_r][target_c] == cur.grid[target_r][target_c + 1] &&
                             cur.grid[target_r][target_c] == initial_grid[pr1][pc1]);

            if (at_target)
                return {static_cast<int>(cur.path.size()), cur.path};
        }

#pragma omp parallel
        {
            vector<State> local_beam;
#pragma omp for schedule(dynamic) nowait
            for (int beam_idx = 0; beam_idx < (int)current_beam.size(); ++beam_idx)
            {
                const auto &cur = current_beam[beam_idx];
                for (int k = 2; k <= N - 1; ++k)
                    for (int r = 0; r <= N - k; ++r)
                        for (int c = 0; c <= N - k; ++c)
                        {
                            if (!Check_Valid(r + cnt, c + cnt, k - 1))
                                continue;
                            bool affects = false;
                            if (r <= pr1 && pr1 < r + k && c <= pc1 && pc1 < c + k)
                                affects = true;
                            if (r <= pr2 && pr2 < r + k && c <= pc2 && pc2 < c + k)
                                affects = true;
                            if (r <= target_r && target_r < r + k && c <= target_c && target_c < c + k)
                                affects = true;
                            if (!affects)
                                continue;
                            vector<vector<uint16_t>> new_grid = rotate_submatrix(cur.grid, k, r, c);
                            vector<Rotation> new_path = cur.path;
                            new_path.emplace_back(k, r, c);
                            local_beam.push_back({new_grid, new_path, {r - c + cur.pos.second, r + c + k - 1 - cur.pos.first}});
                        }
            }
#pragma omp critical
            {
                next_beam.insert(next_beam.end(), local_beam.begin(), local_beam.end());
            }
        }

        sort(next_beam.begin(), next_beam.end(), [&](const State &a, const State &b)
             {
            int da = abs(a.pos.first - target_r) + abs(a.pos.second - target_c) + 2 * a.path.size();
            int db = abs(b.pos.first - target_r) + abs(b.pos.second - target_c) + 2 * b.path.size();
            return da < db; });
        if (next_beam.size() > static_cast<size_t>(beam_width))
            next_beam.resize(beam_width);
        current_beam = std::move(next_beam);
        if (current_beam.empty())
            break;
    }
    return {999, {}};
}

pair<int, vector<Rotation>> search_pair(const vector<vector<uint16_t>> &initial_grid,
                                        int row1, int col1, int row2, int col2,
                                        int mnr, int mxr, int mnc, int mxc)
{
    const int beam_width = 30 + cnt * 5;
    const int max_depth = 3;
    int N = initial_grid.size();

    vector<State> current_beam = {{initial_grid, {}, find_pos(initial_grid, row1, col1)}};
    if (current_beam[0].pos.first == -1)
    {
        broke = true;
        return {1000, {}};
    }

    for (int depth = 0; depth < max_depth; ++depth)
    {
        vector<State> next_beam;

        for (const auto &cur : current_beam)
            if (cur.grid[row1][col1] == cur.grid[row2][col2])
                return {static_cast<int>(cur.path.size()), cur.path};

#pragma omp parallel
        {
            vector<State> local_beam;
#pragma omp for schedule(dynamic) nowait
            for (int beam_idx = 0; beam_idx < (int)current_beam.size(); ++beam_idx)
            {
                const auto &cur = current_beam[beam_idx];
                for (int k = min(max(abs(cur.pos.first - row2), abs(cur.pos.second - col2)) + 2, 24);
                     k >= 2; --k)
                    for (int r = mnr; r <= mxr + 1 - k; ++r)
                        for (int c = mnc; c <= mxc + 1 - k; ++c)
                        {
                            if ((r <= row1 && row1 < r + k && c <= col1 && col1 < c + k) ||
                                !Check_Valid(r + cnt, c + cnt, k - 1))
                                continue;
                            bool affects = (r <= row2 && row2 < r + k && c <= col2 && col2 < c + k);
                            if (!affects)
                                continue;
                            vector<vector<uint16_t>> new_grid = rotate_submatrix(cur.grid, k, r, c);
                            vector<Rotation> new_path = cur.path;
                            new_path.emplace_back(k, r, c);
                            local_beam.push_back({new_grid, new_path, {r - c + cur.pos.second, r + c + k - 1 - cur.pos.first}});
                        }
            }
#pragma omp critical
            {
                next_beam.insert(next_beam.end(), local_beam.begin(), local_beam.end());
            }
        }

        sort(next_beam.begin(), next_beam.end(), [&](const State &a, const State &b)
             {
            int da = abs(a.pos.first - row2) + abs(a.pos.second - col2) + 2 * a.path.size();
            int db = abs(b.pos.first - row2) + abs(b.pos.second - col2) + 2 * b.path.size();
            return da < db; });
        if (next_beam.size() > static_cast<size_t>(beam_width))
            next_beam.resize(beam_width);
        current_beam = std::move(next_beam);
        if (current_beam.empty())
            break;
    }
    return {1000, {}};
}

ExtendedPair extend_free_pair(const vector<vector<uint16_t>> &initial_grid,
                              int pr1, int pr2, int pc1, int pc2,
                              bool tl, bool bl, bool tr, bool br, bool is_vertical)
{
    pair<int, vector<Rotation>> temp;
    struct ExtendedPair result;
    result.cost = 1000;
    int N = initial_grid.size();

    if (is_vertical)
    {
        if (tl && pc1 - 1 >= 0)
        {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr2, pc1 - 1, pr1, pc1 - 1, 0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if (temp.first < result.cost)
            {
                result.cost = temp.first;
                result.position = {pr1, pc1 - 1, pr2, pc1};
                result.path = temp.second;
            }
        }
        if (bl && pc1 - 1 >= 0)
        {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr1, pc1 - 1, pr2, pc1 - 1, 0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if (temp.first < result.cost)
            {
                result.cost = temp.first;
                result.position = {pr1, pc1 - 1, pr2, pc1};
                result.path = temp.second;
            }
        }
        if (tr && pc1 + 1 < N)
        {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr2, pc1 + 1, pr1, pc1 + 1, 0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if (temp.first < result.cost)
            {
                result.cost = temp.first;
                result.position = {pr1, pc1, pr2, pc1 + 1};
                result.path = temp.second;
            }
        }
        if (br && pc1 + 1 < N)
        {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr1, pc1 + 1, pr2, pc1 + 1, 0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if (temp.first < result.cost)
            {
                result.cost = temp.first;
                result.position = {pr1, pc1, pr2, pc1 + 1};
                result.path = temp.second;
            }
        }
    }
    else
    {
        if (tl && pr1 - 1 >= 0)
        {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr1 - 1, pc2, pr1 - 1, pc1, 0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if (temp.first < result.cost)
            {
                result.cost = temp.first;
                result.position = {pr1 - 1, pc1, pr1, pc2};
                result.path = temp.second;
            }
        }
        if (bl && pr1 + 1 < N)
        {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr1 + 1, pc2, pr1 + 1, pc1, 0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if (temp.first < result.cost)
            {
                result.cost = temp.first;
                result.position = {pr1, pc1, pr1 + 1, pc2};
                result.path = temp.second;
            }
        }
        if (tr && pr1 - 1 >= 0)
        {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr1 - 1, pc1, pr1 - 1, pc2, 0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if (temp.first < result.cost)
            {
                result.cost = temp.first;
                result.position = {pr1 - 1, pc1, pr1, pc2};
                result.path = temp.second;
            }
        }
        if (br && pr1 + 1 < N)
        {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr1 + 1, pc1, pr1 + 1, pc2, 0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if (temp.first < result.cost)
            {
                result.cost = temp.first;
                result.position = {pr1, pc1, pr1 + 1, pc2};
                result.path = temp.second;
            }
        }
    }

    if (result.cost == 1000)
    {
        result.position = {-1, -1, -1, -1};
        result.path = {};
    }
    return result;
}

pair<int, vector<Rotation>> move_extend_free_pair(const vector<vector<uint16_t>> &initial_grid,
                                                  int tlr, int tlc, int brr, int brc,
                                                  int target_r, int target_c)
{
    const int beam_width = 20;
    const int max_depth = 3;
    int N = initial_grid.size();
    vector<State> current_beam = {{initial_grid, {}, {tlr, tlc}}};

    for (int depth = 0; depth < max_depth; depth++)
    {
        vector<State> next_beam;
        for (const auto &cur : current_beam)
            if (target_r == cur.pos.first && target_c == cur.pos.second)
                return {static_cast<int>(cur.path.size()), cur.path};

#pragma omp parallel
        {
            vector<State> local_beam;
#pragma omp for schedule(dynamic) nowait
            for (int beam_idx = 0; beam_idx < (int)current_beam.size(); beam_idx++)
            {
                const auto &cur = current_beam[beam_idx];
                for (int k = 2; k <= N - 1; ++k)
                    for (int r = 0; r <= N - k; ++r)
                        for (int c = 0; c <= N - k; ++c)
                        {
                            if (!Check_Valid(r + cnt, c + cnt, k - 1))
                                continue;
                            bool affects = false;
                            auto [c_tlr, c_tlc] = cur.pos;
                            if (c_tlr >= r && c_tlc >= c && c_tlr + 1 < r + k && c_tlc + 1 < c + k)
                                affects = true;
                            if (!affects)
                                continue;
                            vector<vector<uint16_t>> new_grid = rotate_submatrix(cur.grid, k, r, c);
                            vector<Rotation> new_path = cur.path;
                            new_path.emplace_back(k, r, c);
                            local_beam.push_back({new_grid, new_path, {r - c + cur.pos.second, r + c + k - 2 - cur.pos.first}});
                        }
            }
#pragma omp critical
            {
                next_beam.insert(next_beam.end(), local_beam.begin(), local_beam.end());
            }
        }

        sort(next_beam.begin(), next_beam.end(), [&](const State &a, const State &b)
             {
            int da = abs(a.pos.first - target_r) + abs(a.pos.second - target_c) + 2 * a.path.size();
            int db = abs(b.pos.first - target_r) + abs(b.pos.second - target_c) + 2 * b.path.size();
            return da < db; });
        if (next_beam.size() > static_cast<size_t>(beam_width))
            next_beam.resize(beam_width);
        current_beam = std::move(next_beam);
        if (current_beam.empty())
            break;
    }
    return {1000, {}};
}
bool sortBySecond(tuple<int, int, int, int, uint16_t> &a, tuple<int, int, int, int, uint16_t> &b)
{
    if (get<1>(a) < get<1>(b))
        return true;
    return get<0>(a) < get<0>(b);
}
pair<int, vector<Rotation>> Vertical_place(vector<vector<uint16_t>> grid, int Fsize, int j)
{
    int row = Fsize / 2 - 2;
    int min_ops = 999;
    vector<Rotation> partial_result;
    if (grid[row][j] == grid[row + 1][j])
        return {0, {}};

    pair<int, vector<Rotation>> temp_d = search_pair(grid, row, j, row + 1, j, 0, Fsize - 1, 0, Fsize - 1);
    if (temp_d.first < min_ops)
    {
        partial_result = temp_d.second;
        min_ops = temp_d.first;
    }
    temp_d = search_pair(grid, row + 1, j, row, j, 0, Fsize - 1, 0, Fsize - 1);
    if (temp_d.first < min_ops)
    {
        partial_result = temp_d.second;
        min_ops = temp_d.first;
    }
    if (min_ops == 1)
        return {min_ops, partial_result};

    if (SET_FP)
    {

        auto free_pairs = find_free_pairs(grid, j);

        sort(free_pairs.begin(), free_pairs.end(), sortBySecond);

        cout << "V Found " << free_pairs.size() << " free pairs. ";

        for (const auto &[pr1, pc1, pr2, pc2, val] : free_pairs)
        {

            if (pr1 <= row)
                continue;
            int min_col = min(pc1, pc2);
            if (pr1 == pr2 && min_col >= j)
            {
                int k = pr1 - row + min_col - j + 1, r = row - (min_col - j);
                if (r < 0 || j + k >= Fsize || k <= 1)
                    continue;
                if (locked[r + cnt][j + cnt + k - 1] || locked[r + cnt + k - 1][j + cnt + k - 1])
                    continue;
                partial_result.clear();
                partial_result.emplace_back(k, r, j);
                cout << "Using free pair (" << pr1 << "," << pc1 << ")-(" << pr2 << "," << pc2 << ")";
                return {1, partial_result};
            }
        }
    }

    for (int step = j; step <= Fsize / 2 - 4 + j; step++)
    {
        for (int i = row + 1; i <= min(row + Fsize - 3 - step, Fsize - 3); i++)
        {
            auto [ops1, path1] = search_pair(grid, i, step + 1, i, step, 0, Fsize - 1, 0, Fsize - 1);
            if (ops1 != 1)
            {
                auto [ops1t, path1t] = search_pair(grid, i, step, i, step + 1, 0, Fsize - 1, 0, Fsize - 1);
                if (ops1t != 1)
                    continue;
                ops1 = ops1t;
                path1 = path1t;
            }
            if (ops1 == 1)
            {
                int k = i - row + step - j + 1, r = row - (step - j);
                if (r < 0 || j + k >= Fsize)
                {
                    continue;
                }
                if (locked[r + cnt][j + cnt + k - 1] || locked[r + cnt + k - 1][j + cnt + k - 1])
                {
                    continue;
                }
                partial_result = path1;
                partial_result.emplace_back(k, r, j);

                return {2, partial_result};
            }
        }
    }
    return {min_ops, partial_result};
}

pair<int, vector<Rotation>> Horizontal_place(vector<vector<uint16_t>> grid, int Fsize, int j)
{
    int row = Fsize / 2 - 2;
    int min_ops = 999;
    vector<Rotation> partial_result, temp_result;
    auto free_pairs = find_free_pairs(grid, j);
    cout << "H Found " << free_pairs.size() << " free pairs. ";
    SET_FP = true;

    // Extended free pair search
    if (SET_FP)
    {
        for (const auto &[pr1, pc1, pr2, pc2, val] : free_pairs)
        {
            bool is_upper = max(pr1, pr2) < (Fsize / 2 - 2);
            bool is_significant = !(locked[row + cnt][cnt + pc1] || locked[row + cnt][cnt + pc2] ||
                                    locked[row + 1 + cnt][cnt + pc1] || locked[row + 1 + cnt][cnt + pc2]);

            if (is_significant)
            {
                bool is_vertical_pair = pc1 == pc2;
                bool tl = true, bl = true, tr = true, br = true;
                ExtendedPair extend_result;

                if (is_vertical_pair)
                {
                    tl = (pr1 >= 0 && pc1 - 1 >= 0 && !locked[pr1 + cnt][pc1 - 1 + cnt] && !locked[pr2 + cnt][pc1 - 1 + cnt]);
                    bl = (pr2 < Fsize && pc1 - 1 >= 0 && !locked[pr1 + cnt][pc1 - 1 + cnt] && !locked[pr2 + cnt][pc1 - 1 + cnt]);
                    tr = (pr1 >= 0 && pc1 + 1 < Fsize && !locked[pr1 + cnt][pc1 + 1 + cnt] && !locked[pr2 + cnt][pc1 + 1 + cnt]);
                    br = (pr2 < Fsize && pc1 + 1 < Fsize && !locked[pr1 + cnt][pc1 + 1 + cnt] && !locked[pr2 + cnt][pc1 + 1 + cnt]);
                    if (tl || tr || bl || br)
                        extend_result = extend_free_pair(grid, pr1, pr2, pc1, pc2, tl, bl, tr, br, true);
                }
                else
                {
                    tl = (pr1 - 1 >= 0 && pc1 >= 0 && !locked[pr1 - 1 + cnt][pc1 + cnt] && !locked[pr1 - 1 + cnt][pc2 + cnt]);
                    tr = (pr1 - 1 >= 0 && pc2 < Fsize && !locked[pr1 - 1 + cnt][pc1 + cnt] && !locked[pr1 - 1 + cnt][pc2 + cnt]);
                    bl = (pr1 + 1 < Fsize && pc1 >= 0 && !locked[pr1 + 1 + cnt][pc1 + cnt] && !locked[pr1 + 1 + cnt][pc2 + cnt]);
                    br = (pr1 + 1 < Fsize && pc2 < Fsize && !locked[pr1 + 1 + cnt][pc1 + cnt] && !locked[pr1 + 1 + cnt][pc2 + cnt]);
                    if (tl || tr || bl || br)
                        extend_result = extend_free_pair(grid, pr1, pr2, pc1, pc2, tl, bl, tr, br, false);
                }

                auto [tlr, tlc, brr, brc] = extend_result.position;
                if (DEBUG)
                    cout << "EXT RES: " << extend_result.cost << " ";

                if (tlr != -1 && tlc != -1 && brr != -1 && brc != -1)
                {
                    auto move_result = move_extend_free_pair(apply_rotations(grid, extend_result.path), tlr, tlc, brr, brc, row, j);
                    extend_result.path.insert(extend_result.path.end(), move_result.second.begin(), move_result.second.end());
                    extend_result.cost += move_result.first;

                    if (extend_result.cost < min_ops)
                    {
                        partial_result = extend_result.path;
                        min_ops = extend_result.cost;
                        cout << "Using extend free pair (" << tlr << "," << tlc << ")-(" << brr << "," << brc << ") with cost " << min_ops << ". ";
                        ext_pair = true;
                    }
                }
            }
        }
    }
    if (min_ops <= 2)
        return {min_ops, partial_result};

    // First 1
    {
        int mn_free = 999;
        for (const auto &[pr1, pc1, pr2, pc2, val] : free_pairs)
        {
            auto move_result = move_free_pair_to_target(grid, pr1, pc1, pr2, pc2, row, j, false);
            if (move_result.first < mn_free)
            {
                temp_result = move_result.second;
                mn_free = move_result.first;
            }
        }
        auto [ops1, path1] = search_pair(grid, row, j, row, j + 1, 0, Fsize - 1, 0, Fsize - 1);
        locked[cnt + row][cnt + j] = 1;
        locked[cnt + row][cnt + j + 1] = 1;
        if (ops1 > mn_free)
        {
            ops1 = mn_free;
            path1 = temp_result;
        }
        auto [ops2, path2] = search_pair(apply_rotations(grid, path1), row + 1, j, row + 1, j + 1, 0, Fsize - 1, 0, Fsize - 1);
        if(ops1 + ops2 < min_ops) {
            min_ops = ops1 + ops2;
            partial_result = path1;
            partial_result.insert(partial_result.end(), path2.begin(), path2.end());
            ext_pair = false;
        }
        locked[cnt + row][cnt + j] = 0;
        locked[cnt + row][cnt + j + 1] = 0;
        min_ops = ops1 + ops2;
    }
    if (min_ops <= 2)
        return {min_ops, partial_result};

    // First 2
    {
        int mn_free = 999;
        for (const auto &[pr1, pc1, pr2, pc2, val] : free_pairs)
        {
            auto move_result = move_free_pair_to_target(grid, pr1, pc1, pr2, pc2, row + 1, j, false);
            if (move_result.first < mn_free)
            {
                temp_result = move_result.second;
                mn_free = move_result.first;
            }
        }
        auto [ops1, path1] = search_pair(grid, row + 1, j, row + 1, j + 1, 0, Fsize - 1, 0, Fsize - 1);
        locked[cnt + row + 1][cnt + j] = 1;
        locked[cnt + row + 1][cnt + j + 1] = 1;
        if (ops1 > mn_free)
        {
            ops1 = mn_free;
            path1 = temp_result;
        }
        auto [ops2, path2] = search_pair(apply_rotations(grid, path1), row, j, row, j + 1, 0, Fsize - 1, 0, Fsize - 1);
        if (ops1 + ops2 < min_ops)
        {
            min_ops = ops1 + ops2;
            partial_result = path1;
            partial_result.insert(partial_result.end(), path2.begin(), path2.end());
            ext_pair = false;
        }
        locked[cnt + row + 1][cnt + j] = 0;
        locked[cnt + row + 1][cnt + j + 1] = 0;
    }
    if (min_ops <= 2)
        return {min_ops, partial_result};

    int up = (!locked[cnt][cnt + Fsize / 2]) * 2,
        down = (!locked[cnt + Fsize - 1][cnt + Fsize / 2]) * (2 + (j >= row - 1) * (row - j - 2));
    if (j == row + 1)
        down = -1;

    // Down 1
    {
        SET_FP = false;
        for (int step = row + 1; step < Fsize - 3 + down; step++)
        {
            auto [ops1, path1] = search_pair(grid, step, j, step + 1, j, 0, Fsize - 1, 0, Fsize - 1);
            if (ops1 >= 2)
            {
                auto [ops1t, path1t] = search_pair(grid, step + 1, j, step, j, 0, Fsize - 1, 0, Fsize - 1);
                if (ops1t < ops1)
                {
                    ops1 = ops1t;
                    path1 = path1t;
                }
            }
            locked[cnt + step][cnt + j] = 1;
            locked[cnt + step + 1][cnt + j] = 1;
            auto grid_temp = apply_rotations(grid, path1);
            auto [ops2, path2] = search_pair(grid_temp, step, j + 1, step + 1, j + 1, 0, Fsize - 1, 0, Fsize - 1);
            if (ops2 >= 2)
            {
                auto [ops2t, path2t] = search_pair(grid_temp, step + 1, j + 1, step, j + 1, 0, Fsize - 1, 0, Fsize - 1);
                if (ops2t < ops2)
                {
                    ops2 = ops2t;
                    path2 = path2t;
                }
            }
            if (ops1 + ops2 + 1 < min_ops)
            {
                min_ops = ops1 + ops2 + 1;
                partial_result = path1;
                partial_result.insert(partial_result.end(), path2.begin(), path2.end());
                partial_result.emplace_back(step - row + 2, row, j);
                ext_pair = false;
            }
            locked[cnt + step][cnt + j] = 0;
            locked[cnt + step + 1][cnt + j] = 0;
        }
    }

    // Down 2
    {
        SET_FP = false;
        for (int step = row + 1; step < Fsize - 3 + down; step++)
        {
            auto [ops1, path1] = search_pair(grid, step, j + 1, step + 1, j + 1, 0, Fsize - 1, 0, Fsize - 1);
            if (ops1 >= 2)
            {
                auto [ops1t, path1t] = search_pair(grid, step + 1, j + 1, step, j + 1, 0, Fsize - 1, 0, Fsize - 1);
                if (ops1t < ops1)
                {
                    ops1 = ops1t;
                    path1 = path1t;
                }
            }
            locked[cnt + step][cnt + j + 1] = 1;
            locked[cnt + step + 1][cnt + j + 1] = 1;
            auto grid_temp = apply_rotations(grid, path1);
            auto [ops2, path2] = search_pair(grid_temp, step, j, step + 1, j, 0, Fsize - 1, 0, Fsize - 1);
            if (ops2 >= 2)
            {
                auto [ops2t, path2t] = search_pair(grid_temp, step + 1, j, step, j, 0, Fsize - 1, 0, Fsize - 1);
                if (ops2t < ops2)
                {
                    ops2 = ops2t;
                    path2 = path2t;
                }
            }
            if (ops1 + ops2 + 1 < min_ops)
            {
                min_ops = ops1 + ops2 + 1;
                partial_result = path1;
                partial_result.insert(partial_result.end(), path2.begin(), path2.end());
                partial_result.emplace_back(step - row + 2, row, j);
                ext_pair = false;
            }
            locked[cnt + step][cnt + j + 1] = 0;
            locked[cnt + step + 1][cnt + j + 1] = 0;
        }
    }

    if (min_ops <= 3)
    {
        SET_FP = true;
        return {min_ops, partial_result};
    }

    // Down 3
    {
        SET_FP = false;
        for (int step = row + 1; step < Fsize - 3 + down; step++)
        {
            auto [ops1, path1] = search_pair(grid, step + 1, j, step + 1, j + 1, 0, Fsize - 1, 0, Fsize - 1);
            if (ops1 >= 2)
            {
                auto [ops1t, path1t] = search_pair(grid, step + 1, j + 1, step + 1, j, 0, Fsize - 1, 0, Fsize - 1);
                if (ops1t < ops1)
                {
                    ops1 = ops1t;
                    path1 = path1t;
                }
            }
            locked[cnt + step + 1][cnt + j] = 1;
            locked[cnt + step + 1][cnt + j + 1] = 1;
            auto [ops2, path2] = search_pair(apply_rotations(grid, path1), step, j, step, j + 1, 0, Fsize - 1, 0, Fsize - 1);
            if (ops1 + ops2 + 1 < min_ops)
            {
                min_ops = ops1 + ops2 + 1;
                partial_result = path1;
                partial_result.insert(partial_result.end(), path2.begin(), path2.end());
                partial_result.emplace_back(step - row + 2, row, j);
                ext_pair = false;
            }
            locked[cnt + step + 1][cnt + j] = 0;
            locked[cnt + step + 1][cnt + j + 1] = 0;
        }
    }
    if (min_ops <= 3)
        return {min_ops, partial_result};

    // First 3
    {
        auto [ops1, path1] = Vertical_place(grid, Fsize, j);

        locked[cnt + row][cnt + j] = 1;
        locked[cnt + row + 1][cnt + j] = 1;
        auto grid_temp = apply_rotations(grid, path1);
        auto [ops2, path2] = Vertical_place(grid_temp, Fsize, j + 1);

        if (ops1 + ops2 < min_ops)
        {
            min_ops = ops1 + ops2;
            partial_result = path1;
            partial_result.insert(partial_result.end(), path2.begin(), path2.end());
            ext_pair = false;
        }
        locked[cnt + row][cnt + j] = 0;
        locked[cnt + row + 1][cnt + j] = 0;
    }

    SET_FP = true;

    return {min_ops, partial_result};
}

int weighted_free_pairs(const vector<vector<uint16_t>> &crop, int Fsize, int local_cnt)
{
    int N = crop.size();
    int half = N / 2;
    int mid_row_start = half - 2; // n/2 - 1
    int mid_row_end = half - 1;   // n/2
    int score = 0;

    auto get_weight = [&](int row, int col, int mode) -> int
    {
        // Check if in middle rows
        bool in_mid_rows = (row == mid_row_start || row == mid_row_end);

        // Check if upper or lower
        bool is_upper = (row <= mid_row_start);
        bool is_lower = (row >= mid_row_end);

        // Check column zones
        bool is_center = (col >= half - 2 + local_cnt && col <= half + 1 + local_cnt);
        bool is_right = (col >= half + local_cnt);
        bool is_left = (col < half + local_cnt);

        if (mode)
        {
            // Priority 1 : middle rows
            if (row == mid_row_start)
                return 6;

            // // Lower rows
            // if (is_lower)
            // {
            //     if (is_center)
            //         return 4; // Priority 2
            //     if (is_right)
            //         return 1; // Priority 3
            //     if (is_left)
            //         return 4; // Priority 4
            // }

            // // Upper rows
            // if (is_upper)
            // {
            //     if (mid_row_start - 1 == row)
            //     {
            //         return 3;
            //     }
            //     else
            //     {
            //         return 1;
            //     }
            // }
            if (mid_row_start - 1 == row)
            {
                return 6;
            }
            return 1;
        }
        else
        {

            // Horizontal

            if (row == mid_row_end)
                return 6;
            // Lower rows
            if (is_lower)
            {
                if (is_center)
                    return 8; 
                if (is_right)
                    return 3; 
                if (is_left)
                    return 8; 
            }

            // Upper rows
            if (is_upper)
            {
                return 1; 
            }
        }

        // Default (should not reach here)
        return 1;
    };

    set<int> sr;
    // First pass: check if there are same number in given area (weight 1)
    // for (int j = local_cnt; j < min(N , local_cnt + Fsize / 3); j++)
    // {
    //     for (int i = mid_row_start; i < N ; i++)
    //     {
    //         if (locked[cnt + i][cnt + j])
    //             continue;
    //         if (auto search = sr.find(crop[i][j]); search != sr.end())
    //         {
    //             pair<int, int> pos = find_pos(crop, i, j);
    //             if (abs(pos.first - i) <= 1 || abs(pos.second - j) <= 1)
    //                 score += 1;
    //         }
    //         else
    //         {
    //             if (j == local_cnt || j == local_cnt + 1)
    //                 sr.insert(crop[i][j]);
    //         }
    //     }
    // }

    // Second pass: count regular pairs with positional weights
    for (int j = local_cnt; j < N; j++)
    {
        for (int i = mid_row_start; i < N; i++)
        {
            // Skip locked cells (translate to global coords using global cnt)
            if (locked[cnt + i][cnt + j])
                continue;

            // Horizontal pair: (i,j)-(i,j+1)
            if (j + 1 < N && !locked[cnt + i][cnt + j + 1] && crop[i][j] == crop[i][j + 1])
            {
                // For horizontal pairs, use the row and leftmost column
                int w = get_weight(i, j, 0);
                // int cof = 1;
                // if(j == local_cnt)cof =4;
                // if (j == local_cnt+1)
                //     cof = 2;
                // if (j == local_cnt+2)
                //     cof = 2;

                score += w;
                break;
            }
        }
    }
    for (int i = mid_row_start - 1; i < N; i++)
    {
        for (int j = local_cnt; j < N; j++)
        {
            // Skip locked cells (translate to global coords using global cnt)
            if (locked[cnt + i][cnt + j]||(j==local_cnt&&i ==mid_row_start - 1))
                continue;

            // Vertical pair: (i,j)-(i+1,j)
            if (i + 1 < N && !locked[cnt + i + 1][cnt + j] && crop[i][j] == crop[i + 1][j])
            {
                int w = get_weight(i, j, 1);

                if(j==local_cnt&&i==mid_row_start)score+=18;
                score += w;
            }
        }
    }
    return score;
}

// Returns rotations in GLOBAL coordinates (i += cnt, j += cnt already applied).
vector<Rotation> pre_step_beam_search(const vector<vector<uint16_t>> &crop, int Fsize, int PD = 0)
{
    const int MAX_DEPTH = (Fsize + 7) / 8;
    const int BEAM_WIDTH = 300;
    int N = crop.size(); // = Fsize

    cout << "[PreBeam] Starting on " << N << "x" << N
         << " crop (cnt=" << cnt << ", Fsize=" << Fsize << ")\n";

    struct CropState
    {
        vector<vector<uint16_t>> grid;
        vector<Rotation> path; // local coords
        int score;
    };

    auto make_score = [&](const vector<vector<uint16_t>> &g)
    {
        return weighted_free_pairs(g, Fsize, PD);
    };

    int init_score = make_score(crop);
    cout << "[PreBeam] Initial weighted free-pair score: " << init_score << "\n";

    vector<CropState> beam = {{crop, {}, init_score}};
    vector<CropState> best_seen = beam;
    int best_score = init_score;

    for (int depth = 0; depth < MAX_DEPTH; depth++)
    {
        vector<CropState> next_beam;

#pragma omp parallel
        {
            vector<CropState> local_next;

#pragma omp for schedule(dynamic) nowait
            for (int bi = 0; bi < (int)beam.size(); bi++)
            {
                const auto &cur = beam[bi];

                for (int k = 2; k <= N / 2; k++)
                {
                    for (int r = 0; r <= N - k; r++)
                    {
                        for (int c = 0; c <= N - k; c++)
                        {
                            // Check validity using global locked coords
                            if (!Check_Valid(r + cnt, c + cnt, k - 1))
                                continue;

                            vector<vector<uint16_t>> new_grid =
                                rotate_submatrix(cur.grid, k, r, c);
                            int new_score = make_score(new_grid);

                            vector<Rotation> new_path = cur.path;
                            new_path.emplace_back(k, r, c); // local coords

                            local_next.push_back({new_grid, new_path, new_score});
                        }
                    }
                }
            }

#pragma omp critical
            {
                next_beam.insert(next_beam.end(), local_next.begin(), local_next.end());
            }
        }

        if (next_beam.empty())
            break;

        // Sort by score descending
        sort(next_beam.begin(), next_beam.end(),
             [](const CropState &a, const CropState &b)
             { return a.score > b.score; });

        // Trim to beam width
        if ((int)next_beam.size() > BEAM_WIDTH)
            next_beam.resize(BEAM_WIDTH);

        beam = std::move(next_beam);

        if (beam[0].score > best_score)
        {
            best_score = beam[0].score;
            best_seen = {beam[0]};
            cout << "[PreBeam] Depth " << depth + 1
                 << ": improved score to " << best_score << "\n";
        }
        else
        {
            cout << "[PreBeam] Depth " << depth + 1
                 << ": no improvement (best=" << best_score << ")\n";
        }
    }

    // Pick the best state found
    CropState &winner = best_seen[0];
    cout << "[PreBeam] Done. Score: " << init_score << " -> " << winner.score
         << " in " << winner.path.size() << " rotations\n";

    // Translate local coords -> global coords
    vector<Rotation> global_path;
    global_path.reserve(winner.path.size());
    for (auto &rot : winner.path)
        global_path.emplace_back(rot.k, rot.i + cnt, rot.j + cnt);

    return global_path;
}

pair<vector<vector<uint16_t>>, vector<Rotation>> STEP_Do(int Fsize, vector<vector<uint16_t>> grid, int mode)
{
    int half = Fsize / 2;
    int row = half - 2;
    vector<int> dp(half + 2, 0);

    cout << "\n=== Beam search prep (Fsize=" << Fsize << ") ===\n";
    vector<Rotation> pre_path = pre_step_beam_search(grid, Fsize);
    {

        if (!pre_path.empty())
        {
            // Apply to crop in LOCAL coords
            vector<Rotation> local_path;
            local_path.reserve(pre_path.size());
            for (const auto &rot : pre_path)
                local_path.emplace_back(rot.k, rot.i - cnt, rot.j - cnt);
            grid = apply_rotations(grid, local_path);

            // Record in partial_path with GLOBAL coords
            cout << "Applied " << pre_path.size() << " rotations\n";
        }
        cout << "==========================================\n\n";
    }
    vector<pair<vector<vector<uint16_t>>, vector<Rotation>>> result(half + 1, {grid, pre_path});

    pair<int, vector<Rotation>> V_result = Vertical_place(result[mode].first, Fsize, mode);
    int V_ops = dp[mode] + V_result.first;
    locked[cnt + row][cnt + mode] = 1;
    locked[cnt + row + 1][cnt + mode] = 1;

    dp[mode + 1] = V_ops;
    result[mode + 1].first = apply_rotations(result[mode].first, V_result.second);
    for (auto &rot : V_result.second)
        result[mode + 1].second.emplace_back(rot.k, rot.i + cnt, rot.j + cnt);
    cout << "(f)cost : " << V_ops << '\n';

    for (int j = mode + 2; j <= half; j += 1)
    {

        locked[cnt + row][cnt + j - 2] = 0;
        locked[cnt + row + 1][cnt + j - 2] = 0;
        locked[cnt + row][cnt + j - 1] = 0;
        locked[cnt + row + 1][cnt + j - 1] = 0;
        ext_pair = false;

        pair<int, vector<Rotation>> H_result = Horizontal_place(result[j - 2].first, Fsize, j - 2);
        int H_ops = dp[j - 2] + H_result.first;
        locked[cnt + row][cnt + j - 2] = 1;
        locked[cnt + row + 1][cnt + j - 2] = 1;

        pair<int, vector<Rotation>> V_r = Vertical_place(result[j - 1].first, Fsize, j - 1);
        int V_ops2 = dp[j - 1] + V_r.first;
        locked[cnt + row][cnt + j - 1] = 1;
        locked[cnt + row + 1][cnt + j - 1] = 1;

        if (V_ops2 > H_ops)
        {
            dp[j] = H_ops;
            result[j].first = apply_rotations(result[j - 2].first, H_result.second);
            result[j].second = result[j - 2].second;
            for (auto &rot : H_result.second)
                result[j].second.emplace_back(rot.k, rot.i + cnt, rot.j + cnt);
            cout << "H (f)cost : " << H_result.first;
            if (ext_pair)
                cout << "\tYEAHHHHH\t";
        }
        else
        {
            dp[j] = V_ops2;
            result[j].first = apply_rotations(result[j - 1].first, V_r.second);
            result[j].second = result[j - 1].second;
            for (auto &rot : V_r.second)
                result[j].second.emplace_back(rot.k, rot.i + cnt, rot.j + cnt);
            cout << "V (f)cost : " << V_r.first;
        }
        // if ((mode + half) / 2 == j)
        // {
        //     cout << "\n=== Beam search prep (Fsize=" << Fsize << ") ===\n";
        //     pre_path = pre_step_beam_search(result[j].first, Fsize, j);
        //     {
        //         if (!pre_path.empty())
        //         {
        //             // Apply to crop in LOCAL coords
        //             vector<Rotation> local_path;
        //             local_path.reserve(pre_path.size());
        //             for (const auto &rot : pre_path)
        //                 local_path.emplace_back(rot.k, rot.i - cnt, rot.j - cnt);
        //             result[j].first = apply_rotations(result[j].first, local_path);
        //             result[j].second.insert(result[j].second.end(), pre_path.begin(), pre_path.end());
        //             // Record in partial_path with GLOBAL coords
        //             cout << " Applied " << pre_path.size() << " rotations\n";
        //         }
        //         cout << "==========================================\n\n";
        //     }
        // }
        cout << '\n';
    }

    locked = rotate_submatrix_u8(locked, half, cnt, cnt);
    grid = rotate_submatrix(result[half].first, half, 0, 0);
    result[half].second.emplace_back(half, cnt, cnt);
    cout << "Rotate half\n";

    return {grid, result[half].second};
}

// ─────────────────────────────────────────────────────────────
//  File 2: Beam Search (3-cycle macro + healing + backtracking)
// ─────────────────────────────────────────────────────────────

vector<vector<uint16_t>> apply_macro_cycle(vector<vector<uint16_t>> grid, int k, int i, int j,
                                           vector<Rotation> &path)
{
    for (int step = 0; step < 4; ++step)
    {
        grid = rotate_submatrix(grid, k, i, j + 1);
        path.emplace_back(k, i, j + 1);
        grid = rotate_submatrix(grid, k, i, j);
        path.emplace_back(k, i, j);
    }
    return grid;
}

// ========== MODIFIED: added timeout checks inside healing ==========
vector<GridState> unstuck_healing(const GridState &stuck_state, int num_random_moves)
{
    cout << "HEALING: Applying combination of random moves and 3-cycle macro moves..." << endl;

    vector<GridState> healed_states;
    random_device rd;
    mt19937 gen(rd());

    for (int attempt = 0; attempt < 15; attempt++)
    {
        if (is_time_up()) break;   // <-- new timeout check

        vector<vector<uint16_t>> current_grid = stuck_state.grid;
        vector<Rotation> current_path = stuck_state.path;

        if (attempt < 6)
        {
            for (int move = 0; move < num_random_moves; move++)
            {
                uniform_int_distribution<> k_dist(2, min(8, n));
                int k = k_dist(gen);
                uniform_int_distribution<> i_dist(0, n - k);
                int i = i_dist(gen);
                uniform_int_distribution<> j_dist(0, n - k);
                int j = j_dist(gen);
                current_grid = rotate_submatrix(current_grid, k, i, j);
                current_path.emplace_back(k, i, j);
            }
        }
        else if (attempt < 12)
        {
            int num_macro_cycles = num_random_moves / 2;
            int num_regular_moves = num_random_moves - (num_macro_cycles * 8);

            for (int cycle = 0; cycle < num_macro_cycles; cycle++)
            {
                uniform_int_distribution<> k_dist(2, min(7, n - 1));
                int k = k_dist(gen);
                uniform_int_distribution<> i_dist(0, n - k);
                int i = i_dist(gen);
                uniform_int_distribution<> j_dist(0, n - k - 1);
                int j = j_dist(gen);
                current_grid = apply_macro_cycle(current_grid, k, i, j, current_path);
            }
            for (int move = 0; move < num_regular_moves; move++)
            {
                uniform_int_distribution<> k_dist(2, min(8, n));
                int k = k_dist(gen);
                uniform_int_distribution<> i_dist(0, n - k);
                int i = i_dist(gen);
                uniform_int_distribution<> j_dist(0, n - k);
                int j = j_dist(gen);
                current_grid = rotate_submatrix(current_grid, k, i, j);
                current_path.emplace_back(k, i, j);
            }
        }
        else
        {
            int num_macro_cycles = (num_random_moves + 7) / 8;
            for (int cycle = 0; cycle < num_macro_cycles; cycle++)
            {
                uniform_int_distribution<> k_dist(2, min(7, n - 1));
                int k = k_dist(gen);
                uniform_int_distribution<> i_dist(0, n - k);
                int i = i_dist(gen);
                uniform_int_distribution<> j_dist(0, n - k - 1);
                int j = j_dist(gen);
                current_grid = apply_macro_cycle(current_grid, k, i, j, current_path);
            }
        }

        int new_paired = count_paired_values(current_grid);
        int new_heuristic = calculate_manhattan_heuristic(current_grid);
        healed_states.push_back({current_grid, current_path, new_paired, new_heuristic});
    }

    sort(healed_states.begin(), healed_states.end(), [](const GridState &a, const GridState &b)
         {
        if (a.paired_count != b.paired_count) return a.paired_count > b.paired_count;
        return a.heuristic < b.heuristic; });

    cout << "HEALING: Best healed state has " << healed_states[0].paired_count << " pairs" << endl;
    return healed_states;
}

// beam_search operates on the cropped inner block (inner_n x inner_n).
// 'offset' is the global cnt at the time of calling (used to translate
// crop-local rotation coords back to global coords for the full_path).
// Returns rotations in GLOBAL coordinates (i += offset, j += offset).
vector<Rotation> beam_search(const vector<vector<uint16_t>> &inner_grid, int offset, int max_depth)
{
    int inner_n = inner_grid.size();
    int target_paired = inner_n * inner_n / 2;

    int initial_paired = count_paired_values(inner_grid);
    int initial_heuristic = calculate_manhattan_heuristic(inner_grid);

    int base_beam_width = 1800;
    double beam_multiplier = 1.0;

    int num_threads = omp_get_max_threads();
    cout << "Starting Beam Search on " << inner_n << "x" << inner_n
         << " inner block (offset=" << offset << ", beam=" << base_beam_width
         << ", threads=" << num_threads << ")\n";
    cout << "Initial paired: " << initial_paired << "/" << target_paired << "\n";

    priority_queue<GridState> beam;
    beam.push({inner_grid, {}, initial_paired, initial_heuristic});

    GridState global_best = {inner_grid, {}, initial_paired, initial_heuristic};
    int depth = 0;
    int stuck_counter = 0;
    int last_best_paired = initial_paired;

    vector<StateSnapshot> state_history;
    unordered_map<int, int> paired_count_visits;

    for (; depth < max_depth && !beam.empty(); ++depth)
    {
        if (is_time_up())
        {
            cout << "Time limit reached at depth " << depth << endl;
            for (auto &r : global_best.path)
            {
                r.i += offset;
                r.j += offset;
            }
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
        bool solution_found = false;
        vector<Rotation> solution_path;

#pragma omp parallel
        {
            int thread_id = omp_get_thread_num();
            int local_best_paired = 0;

#pragma omp for schedule(dynamic)
            for (int idx = 0; idx < (int)current_states.size(); idx++)
            {
                if (solution_found)
                    continue;
                GridState current = current_states[idx];

                if (current.paired_count > global_best.paired_count)
#pragma omp critical
                {
                    if (current.paired_count > global_best.paired_count)
                        global_best = current;
                }

                if (is_solved(current.grid))
#pragma omp critical
                {
                    if (!solution_found)
                    {
                        solution_found = true;
                        solution_path = current.path;
                    }
                }

                // Only rotate within the inner block (k up to inner_n)
                for (int k = 2; k <= inner_n - 1; ++k)
                {
                    if (solution_found)
                        break;
                    for (int i = 0; i <= inner_n - k; ++i)
                    {
                        if (solution_found)
                            break;
                        for (int j = 0; j <= inner_n - k; ++j)
                        {
                            if (solution_found)
                                break;

                            // Skip rotations that touch locked border cells
                            // (locked uses global coords: offset + local i/j)
                            if (!Check_Valid(i + offset, j + offset, k - 1))
                                continue;

                            // 3-cycle macro when stuck
                            if (stuck_counter >= 4 && j + 1 <= inner_n - k)
                            {
                                vector<Rotation> macro_path = current.path;
                                vector<vector<uint16_t>> macro_grid =
                                    apply_macro_cycle(current.grid, k, i, j, macro_path);
                                int macro_paired = count_paired_values(macro_grid);
                                if (macro_paired >= current.paired_count)
                                {
                                    int macro_heuristic = calculate_manhattan_heuristic(macro_grid);
                                    if (macro_paired > local_best_paired)
                                        local_best_paired = macro_paired;
                                    if (is_solved(macro_grid))
#pragma omp critical
                                    {
                                        if (!solution_found)
                                        {
                                            solution_found = true;
                                            solution_path = macro_path;
                                        }
                                    }
                                    string mk = get_grid_key(macro_grid);
                                    if (thread_local_visited[thread_id].find(mk) == thread_local_visited[thread_id].end())
                                    {
                                        thread_local_visited[thread_id].insert(mk);
                                        thread_local_states[thread_id].push_back({macro_grid, macro_path, macro_paired, macro_heuristic});
                                    }
                                }
                            }

                            vector<vector<uint16_t>> new_grid = rotate_submatrix(current.grid, k, i, j);
                            int new_paired = count_paired_values(new_grid);
                            if (new_paired < current.paired_count)
                                continue;

                            int new_heuristic = calculate_manhattan_heuristic(new_grid);
                            vector<Rotation> new_path = current.path;
                            new_path.emplace_back(k, i, j); // local coords — translated later

                            if (new_paired > local_best_paired)
                                local_best_paired = new_paired;

                            if (is_solved(new_grid))
#pragma omp critical
                            {
                                if (!solution_found)
                                {
                                    solution_found = true;
                                    solution_path = new_path;
                                }
                            }

                            string gk = get_grid_key(new_grid);
                            if (thread_local_visited[thread_id].find(gk) == thread_local_visited[thread_id].end())
                            {
                                thread_local_visited[thread_id].insert(gk);
                                thread_local_states[thread_id].push_back({new_grid, new_path, new_paired, new_heuristic});
                            }
                        }
                    }
                }

#pragma omp critical
                {
                    if (local_best_paired > best_paired_in_depth)
                        best_paired_in_depth = local_best_paired;
                }
            }
        }

        if (solution_found)
        {
            cout << "Solution found at depth " << depth << endl;
            for (auto &r : solution_path)
            {
                r.i += offset;
                r.j += offset;
            }
            return solution_path;
        }

        cout << "Depth " << depth << ": " << current_states.size()
             << " states, best paired: " << best_paired_in_depth << "/" << target_paired;

        if (best_paired_in_depth > last_best_paired)
        {
            cout << " (IMPROVED!)";
            StateSnapshot snapshot;
            snapshot.beam_states = current_states;
            snapshot.paired_count = best_paired_in_depth;
            snapshot.depth_at_snapshot = depth;
            snapshot.beam_multiplier = beam_multiplier;
            state_history.push_back(snapshot);
            last_best_paired = best_paired_in_depth;
            stuck_counter = 0;
        }
        else
        {
            stuck_counter++;
            cout << " (stuck: " << stuck_counter << "/" << STUCK_LIMIT << ")";   // show new limit
        }
        cout << "\n";

        priority_queue<GridState> next_beam;
        unordered_set<string> global_visited;
        for (int t = 0; t < num_threads; t++)
            for (const auto &state : thread_local_states[t])
            {
                string key = get_grid_key(state.grid);
                if (global_visited.find(key) == global_visited.end())
                {
                    global_visited.insert(key);
                    next_beam.push(state);
                }
            }

        // ========== MODIFIED backtrack logic ==========
        if (stuck_counter >= STUCK_LIMIT)   // now uses STUCK_LIMIT (set to 3)
        {
            if (!state_history.empty())
            {
                cout << "\n!!! BACKTRACKING TRIGGERED !!!" << endl;
                bool found_valid_backtrack = false;
                StateSnapshot backtrack_target;

                // Instead of searching backwards, pick the snapshot that is BACKTRACK_DEPTH steps before the end.
                int target_idx = max(0, (int)state_history.size() - BACKTRACK_DEPTH);
                backtrack_target = state_history[target_idx];

                // Remove all snapshots after this index so we don't revisit them.
                state_history.resize(target_idx);
                found_valid_backtrack = true;

                if (found_valid_backtrack)
                {
                    beam_multiplier = 1.5;   // set multiplier to 1.5 (not multiplied)
                    next_beam = priority_queue<GridState>();
                    for (const auto &state : backtrack_target.beam_states)
                        next_beam.push(state);
                    // Optionally we could push the target back with updated multiplier,
                    // but since we truncated history, we can just continue.
                    stuck_counter = 0;
                    last_best_paired = backtrack_target.paired_count;
                    best_paired_in_depth = backtrack_target.paired_count;
                    cout << "Backtracking done. Beam multiplier: " << beam_multiplier << "\n";
                }
                else
                {
                    cout << "No valid backtrack. Healing...\n";
                    GridState best_cur = current_states[0];
                    for (const auto &s : current_states)
                        if (s.paired_count > best_cur.paired_count)
                            best_cur = s;
                    auto healed = unstuck_healing(best_cur, min(10, inner_n / 2));
                    // ===== new: if healing produced no states due to timeout, break =====
                    if (healed.empty())
                    {
                        cout << "Healing stopped due to timeout. Terminating search.\n";
                        break;
                    }
                    int best_healed = 0;
                    for (const auto &hs : healed)
                    {
                        next_beam.push(hs);
                        best_healed = max(best_healed, hs.paired_count);
                    }
                    if (best_healed > 0)
                    {
                        best_paired_in_depth = best_healed;
                        last_best_paired = best_healed;
                    }
                    stuck_counter = 0;
                }
            }
            else
            {
                cout << "STUCK — no history. Healing...\n";
                GridState best_cur = current_states[0];
                for (const auto &s : current_states)
                    if (s.paired_count > best_cur.paired_count)
                        best_cur = s;
                auto healed = unstuck_healing(best_cur, min(10, inner_n / 2));
                if (healed.empty())
                {
                    cout << "Healing stopped due to timeout. Terminating search.\n";
                    break;
                }
                int best_healed = 0;
                for (const auto &hs : healed)
                {
                    next_beam.push(hs);
                    best_healed = max(best_healed, hs.paired_count);
                }
                if (best_healed > 0)
                {
                    best_paired_in_depth = best_healed;
                    last_best_paired = best_healed;
                }
                stuck_counter = 0;
            }
        }
        int desired_beam_width = base_beam_width;
        if(depth < 40) {
            desired_beam_width -= 1000;
        }
        int current_beam_width = (int)(desired_beam_width * beam_multiplier);
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

        if (kept == 0)
        {
            cout << "No improving moves. Search terminated.\n";
            break;
        }
        cout << "Kept " << kept << " states (beam width: " << current_beam_width << ")\n";
    }

    cout << "Beam search done. Best paired: " << global_best.paired_count << "/" << target_paired << "\n";
    // Translate local-crop coordinates to global coordinates
    for (auto &r : global_best.path)
    {
        r.i += offset;
        r.j += offset;
    }
    return global_best.path;
}

int main()
{
    omp_set_num_threads(omp_get_max_threads());
    cout << "Using " << omp_get_max_threads() << " threads\n";

    vector<vector<uint16_t>> init_grid = get_random_board(24);
    vector<vector<uint16_t>> grid = init_grid;
    n = grid.size();
    locked = vector<vector<uint8_t>>(n, vector<uint8_t>(n, 0));
    vector<Rotation> full_path;

    // ── Phase 1: File 1's frame-by-frame STEP_Do solver ──
    for (int Fsize = n - cnt * 2; Fsize > 12; Fsize -= 4)
    {
        vector<vector<uint16_t>> crop;
        vector<Rotation> partial_path;
        crop.reserve(n - cnt * 2);

        for (int i = cnt; i < n - cnt; i++)
        {
            vector<uint16_t> row(grid[i].begin() + cnt, grid[i].end() - cnt);
            crop.push_back(row);
        }

        // First STEP_Do call (corner 1, part 1)
        pair<vector<vector<uint16_t>>, vector<Rotation>> res = STEP_Do(Fsize, crop, 0);
        partial_path.insert(partial_path.end(), res.second.begin(), res.second.end());

        crop = rotate_submatrix(res.first, Fsize, 0, 0);
        locked = rotate_submatrix_u8(locked, Fsize, cnt, cnt);
        partial_path.emplace_back(Fsize, cnt, cnt);

        // Loop through remaining corners (4 corners total, we already did the first STEP_Do)
        for (int i = 0; i < 3; i++)
        {

            // Second STEP_Do of this corner pair (completes one corner)
            auto res1 = STEP_Do(Fsize, crop, 0);
            partial_path.insert(partial_path.end(), res1.second.begin(), res1.second.end());

            auto res2 = STEP_Do(Fsize, res1.first, 2);
            partial_path.insert(partial_path.end(), res2.second.begin(), res2.second.end());

            crop = rotate_submatrix(res2.first, Fsize, 0, 0);
            locked = rotate_submatrix_u8(locked, Fsize, cnt, cnt);
            partial_path.emplace_back(Fsize, cnt, cnt);
        }

        res = STEP_Do(Fsize, crop, 2);
        partial_path.insert(partial_path.end(), res.second.begin(), res.second.end());

        grid = apply_rotations(grid, partial_path);
        full_path.insert(full_path.end(), partial_path.begin(), partial_path.end());

        cout << "Current Frame cost :" << partial_path.size() << '\n';
        cout << "Current Frame total cost :" << full_path.size() << '\n';
        cout << "------------------------------------\n";
        cout << "Locked: \n";
        print_grid(locked);
        cout << "crop (cnt = " << cnt << " ):\n";
        print_grid(res.first);
        cout << "grid: \n";
        print_grid(grid);
        cout << "------------------------------------\n";

        cnt += 2;
    }

    // ── Phase 2: Beam search on the remaining inner block ──
    // After the STEP_Do loop, cnt has been incremented so the remaining
    // unsolved region is (n - cnt*2) x (n - cnt*2) starting at global offset cnt.
    {
        int inner_n = n - cnt * 2; // e.g. 12 for n=24 after 3 STEP_Do passes
        int offset = cnt;          // global row/col offset of the inner block

        // Extract the inner crop from the current global grid
        vector<vector<uint16_t>> inner_grid;
        inner_grid.reserve(inner_n);
        for (int i = offset; i < offset + inner_n; i++)
        {
            vector<uint16_t> row(grid[i].begin() + offset,
                                 grid[i].begin() + offset + inner_n);
            inner_grid.push_back(row);
        }

        // Check how many pairs are already solved inside the inner block
        int inner_paired = count_paired_values(inner_grid);
        int inner_target = inner_n * inner_n / 2;

        cout << "\n=== Beam Search on " << inner_n << "x" << inner_n
             << " inner block (global offset=" << offset << ") ===\n";
        cout << "Inner pairs solved: " << inner_paired << "/" << inner_target << "\n";

        if (inner_paired < inner_target)
        {
            int max_search_depth = inner_n * inner_n * 2;

            // beam_search returns rotations already in GLOBAL coordinates
            vector<Rotation> beam_path = beam_search(inner_grid, offset, max_search_depth);

            grid = apply_rotations(grid, beam_path);
            full_path.insert(full_path.end(), beam_path.begin(), beam_path.end());

            // Verify the inner result
            vector<vector<uint16_t>> inner_after;
            for (int i = offset; i < offset + inner_n; i++)
            {
                vector<uint16_t> r(grid[i].begin() + offset,
                                   grid[i].begin() + offset + inner_n);
                inner_after.push_back(r);
            }
            cout << "After beam search inner pairs: "
                 << count_paired_values(inner_after) << "/" << inner_target << "\n";
        }
        else
        {
            cout << "Inner block already fully solved — beam search skipped.\n";
        }
    }

    // ── Results ──
    cout << "-----------------  Before  -------------------\n";
    print_grid(init_grid);
    save_file(init_grid, full_path);
    cout << "-----------------  After   -------------------\n";
    init_grid = apply_rotations(init_grid, full_path);
    print_grid(init_grid);
    cout << "\nTotal ops: " << full_path.size() << "\n";
    cout << "Solved: " << (is_solved(init_grid) ? "YES" : "NO") << "\n";

    if (broke)
        cout << "-------------------------------------BROKE----------------------------------\n";

    // ========== NEW: print total elapsed time ==========
    auto end_time = high_resolution_clock::now();
    duration<double> elapsed = end_time - start_time;
    cout << "Total time used: " << elapsed.count() << " seconds\n";
    // ===================================================

    return 0;
}