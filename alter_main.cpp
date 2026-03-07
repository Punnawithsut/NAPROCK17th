#include <bits/stdc++.h>
#include <cpr/cpr.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <omp.h>
#include <random>
#include <unordered_set>

using namespace std;

#ifdef USE_CUDA
#include "cuda_beam_search.cuh"
using GPUBeamSearch = CudaBeamSearch;
#else
#include "metal_beam_search.h"
using GPUBeamSearch = MetalBeamSearch;
#endif

using namespace std::chrono;
using json = nlohmann::json;

struct Rotation
{
    int k, i, j;
    Rotation(int k, int i, int j) : k(k), i(i), j(j) {}
};

struct ExtendedPair
{
    tuple<int, int, int, int> position;
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
const int TEST_WITH_NAPROCK_REAL_SERVER = 1;

// ── Zobrist Hashing ──
static const int ZOBRIST_MAX_N = 64;
static const int ZOBRIST_MAX_VAL = 2048;
uint64_t zobrist_table[ZOBRIST_MAX_N][ZOBRIST_MAX_N][ZOBRIST_MAX_VAL];

void init_zobrist()
{
    std::mt19937_64 rng(0xDEADBEEF42ULL);
    for (int r = 0; r < ZOBRIST_MAX_N; r++)
        for (int c = 0; c < ZOBRIST_MAX_N; c++)
            for (int v = 0; v < ZOBRIST_MAX_VAL; v++)
                zobrist_table[r][c][v] = rng();
}

struct GridState
{
    vector<vector<uint16_t>> grid;
    vector<Rotation> path;
    int paired_count;
    int heuristic;
    uint64_t hash;

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

// ── Server API ──
json fetch_match_data(const string &url, const string &token)
{
    auto response = cpr::Get(cpr::Url{url + "/"},
                             cpr::Parameters{{"token", token}});
    if (response.status_code != 200)
    {
        cerr << "Error fetching data! Status: " << response.status_code << endl;
        cerr << "Server says: " << response.text << endl;
        exit(1);
    }
    return json::parse(response.text);
}

void submit_answer(const string &url, const string &token, const vector<Rotation> &path)
{
    json ops_json = json::array();
    for (const auto &rot : path)
    {
        ops_json.push_back({{"x", rot.j}, {"y", rot.i}, {"n", rot.k}});
    }
    json body = {{"ops", ops_json}};
    auto response = cpr::Post(cpr::Url{url + "/"},
                              cpr::Header{{"Content-Type", "application/json"}},
                              cpr::Parameters{{"token", token}},
                              cpr::Body{body.dump()});
    if (response.status_code == 200)
        cout << "Successfully submitted! Revision: " << json::parse(response.text)["revision"] << endl;
    else
        cerr << "Submission failed! " << response.text << endl;
}

vector<vector<uint16_t>> get_random_board(int n)
{
    json requestBody = {{"boardSize", n}};
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
        rotations_map[rotation_key] = {{"k", fp[k].k}, {"i", fp[k].i}, {"j", fp[k].j}};
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
        cout << "Couldn't open file, save file failed" << endl;
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

uint64_t compute_zobrist_hash(const vector<vector<uint16_t>> &grid)
{
    uint64_t h = 0;
    for (int r = 0; r < (int)grid.size(); r++)
        for (int c = 0; c < (int)grid[r].size(); c++)
            h ^= zobrist_table[r][c][grid[r][c]];
    return h;
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

int weighted_free_pairs(const vector<vector<uint16_t>> &crop, int Fsize, int local_cnt)
{
    int N = crop.size();
    int half = N / 2;
    int mid_row_start = half - 2;
    int mid_row_end = half - 1;
    int score = 0;

    auto get_weight = [&](int row, int col, int mode) -> int
    {
        bool is_upper = (row <= mid_row_start);
        bool is_lower = (row >= mid_row_end);
        bool is_center = (col >= half - 2 + local_cnt && col <= half + 1 + local_cnt);
        bool is_right = (col >= half + local_cnt);
        bool is_left = (col < half + local_cnt);

        if (mode)
        {
            if (row == mid_row_start) return 5;
            if (is_lower)
            {
                if (is_center) return 3;
                if (is_right) return 1;
                if (is_left) return 3;
            }
            if (is_upper)
                return (mid_row_start - 1 == row) ? 3 : 1;
        }
        else
        {
            if (row == mid_row_end) return 7;
            if (is_lower)
            {
                if (is_center) return 8;
                if (is_right) return 4;
                if (is_left) return 8;
            }
            if (is_upper) return 1;
        }
        return 1;
    };

    set<int> sr;
    for (int i = mid_row_start; i < N - 1; i++)
    {
        for (int j = local_cnt; j < min(N - 1, local_cnt + Fsize / 4); j++)
        {
            if (locked[cnt + i][cnt + j]) continue;
            if (auto search = sr.find(crop[i][j]); search != sr.end())
            {
                pair<int, int> pos = find_pos(crop, i, j);
                if (abs(pos.first - i) <= 1 || abs(pos.second - j) <= 1)
                    score += 2;
            }
            else sr.insert(crop[i][j]);
        }
    }

    for (int j = local_cnt; j < N; j++)
        for (int i = mid_row_start; i < N; i++)
        {
            if (locked[cnt + i][cnt + j]) continue;
            if (j + 1 < N && !locked[cnt + i][cnt + j + 1] && crop[i][j] == crop[i][j + 1])
            {
                score += get_weight(i, j, 0);
                break;
            }
        }

    for (int i = mid_row_start - 1; i < N; i++)
        for (int j = local_cnt; j < N; j++)
        {
            if (locked[cnt + i][cnt + j]) continue;
            if (i + 1 < N && !locked[cnt + i + 1][cnt + j] && crop[i][j] == crop[i + 1][j])
                score += get_weight(i, j, 1);
        }

    return score;
}

int weighted_beam_DO(const vector<vector<uint16_t>> &crop, int Fsize, int PD)
{
    int N = crop.size();
    int half = N / 2;
    int mid_row_start = half - 2;
    int mid_row_end = half - 1;
    int score = 0;

    auto get_weight = [&](int row, int col, int mode,int CurJ) -> int
    {
        int cur_half = (CurJ+N)>>1;
        bool is_upper = (row <= mid_row_start);
        bool is_lower = (row >= mid_row_end);
        bool is_right = (col >= cur_half);
        bool is_left = (col < cur_half);

        if (mode)
        {
            if (row == mid_row_start) return 5;
            if (is_lower)
            {
                if (is_right) return 1;
                if (is_left) return 5;
            }
            if (is_upper)
                return (mid_row_start - 1 == row) ? 5 : 1;
        }
        else
        {
            if (row == mid_row_end) return 7;
            if (is_lower)
            {
                if (is_right) return 4;
                if (is_left) return 8;
            }
            if (is_upper) return 1;
        }
        return 1;
    };

    int CUR_J = 0;
    for (int j = PD; j < half;)
    {
        if (crop[mid_row_start][j] == crop[mid_row_end][j])
        {
            j++;
            score += 1000;
        }
        else if (((crop[mid_row_start][j] == crop[mid_row_start][j + 1]) &&
                  (crop[mid_row_end][j] == crop[mid_row_end][j + 1])) &&
                 (j != half - 1))
        {
            j += 2;
            score += 2000;
        }
        else break;
        CUR_J = j;
    }
    if (CUR_J >= half) return 99999;

    set<int> sr;
    for (int i = mid_row_start; i < N - 1; i++)
        for (int j = CUR_J; j < min(N - 1, CUR_J + Fsize / 4); j++)
        {
            if (locked[cnt + i][cnt + j]) continue;
            if (auto search = sr.find(crop[i][j]); search != sr.end())
            {
                pair<int, int> pos = find_pos(crop, i, j);
                if (abs(pos.first - i) <= 1 || abs(pos.second - j) <= 1)
                    score += 2;
            }
            else sr.insert(crop[i][j]);
        }

    for (int j = CUR_J; j < N; j++)
        for (int i = mid_row_start; i < N; i++)
        {
            if (locked[cnt + i][cnt + j]) continue;
            if (j + 1 < N && !locked[cnt + i][cnt + j + 1] && crop[i][j] == crop[i][j + 1])
            {
                score += get_weight(i, j, 0,CUR_J);
                break;
            }
        }

    for (int i = mid_row_start - 1; i < N; i++)
        for (int j = CUR_J; j < N; j++)
        {
            if (locked[cnt + i][cnt + j]) continue;
            if (i + 1 < N && !locked[cnt + i + 1][cnt + j] && crop[i][j] == crop[i + 1][j])
                score += get_weight(i, j, 1,CUR_J);
        }

    return score;
}

// ── Pre-step beam search (GPU-accelerated) ────────────────────────────────────
vector<Rotation> pre_step_beam_search(const vector<vector<uint16_t>> &crop,
                                       int Fsize, int PD,
                                       GPUBeamSearch &gpu_search)
{
    const int MAX_DEPTH  = 2;
    const int BEAM_WIDTH = 100;
    const int N          = (int)crop.size();

    cout << "[PreBeam] Starting on " << N << "x" << N
         << " crop (cnt=" << cnt << ", Fsize=" << Fsize << ")\n";

    struct CropState {
        vector<vector<uint16_t>> grid;
        vector<Rotation>         path;
        int                      score;
    };

    // Pre-compute valid rotations in local crop coordinates
    vector<tuple<int,int,int>> valid_rots;
    for (int k = 2; k <= N / 2; ++k)
        for (int r = 0; r <= N - k; ++r)
            for (int c = 0; c <= N - k; ++c)
                if (Check_Valid(r + cnt, c + cnt, k - 1))
                    valid_rots.emplace_back(k, r, c);

#ifdef USE_CUDA
    const bool use_gpu = (N <= CCB_MAX_N);
    if (!use_gpu)
        cout << "[PreBeam] N=" << N << " > CCB_MAX_N=" << CCB_MAX_N << " — CPU fallback.\n";
#else
    const bool use_gpu = false;
#endif

    auto cpu_score = [&](const vector<vector<uint16_t>> &g) {
        return weighted_free_pairs(g, Fsize, PD);
    };

    int init_score = cpu_score(crop);
    cout << "[PreBeam] Initial weighted free-pair score: " << init_score << "\n";

    vector<CropState> beam      = {{crop, {}, init_score}};
    vector<CropState> best_seen = beam;
    int               best_score = init_score;

    for (int depth = 0; depth < MAX_DEPTH; ++depth)
    {
        vector<CropState> next_beam;

#ifdef USE_CUDA
        if (use_gpu)
        {
            const int S = (int)beam.size();
            vector<uint16_t> flat(S * N * N);
            for (int s = 0; s < S; ++s)
                for (int r = 0; r < N; ++r)
                    for (int c = 0; c < N; ++c)
                        flat[s * N * N + r * N + c] = beam[s].grid[r][c];

            // mode 0 = weighted_free_pairs
            auto results = gpu_search.evaluate_crop(flat, S, N, valid_rots,
                                                    cnt, Fsize, PD, /*mode=*/0);
            next_beam.reserve(results.size());
            for (const auto &res : results)
            {
                const auto &[k, r, c] = valid_rots[res.rot_idx];
                CropState ns;
                ns.grid  = rotate_submatrix(beam[res.parent_idx].grid, k, r, c);
                ns.path  = beam[res.parent_idx].path;
                ns.path.emplace_back(k, r, c);
                ns.score = res.score;
                next_beam.push_back(std::move(ns));
            }
        }
        else
#endif
        {
#pragma omp parallel
            {
                vector<CropState> local_next;
#pragma omp for schedule(dynamic) nowait
                for (int bi = 0; bi < (int)beam.size(); bi++)
                {
                    const auto &cur = beam[bi];
                    for (int k = 2; k <= N / 2; k++)
                        for (int r = 0; r <= N - k; r++)
                            for (int c = 0; c <= N - k; c++)
                            {
                                if (!Check_Valid(r + cnt, c + cnt, k - 1)) continue;
                                auto new_grid  = rotate_submatrix(cur.grid, k, r, c);
                                int  new_score = cpu_score(new_grid);
                                auto new_path  = cur.path;
                                new_path.emplace_back(k, r, c);
                                local_next.push_back({new_grid, new_path, new_score});
                            }
                }
#pragma omp critical
                { next_beam.insert(next_beam.end(), local_next.begin(), local_next.end()); }
            }
        }

        if (next_beam.empty()) break;

        sort(next_beam.begin(), next_beam.end(),
             [](const CropState &a, const CropState &b) { return a.score > b.score; });
        if ((int)next_beam.size() > BEAM_WIDTH) next_beam.resize(BEAM_WIDTH);
        beam = std::move(next_beam);

        if (beam[0].score > best_score)
        {
            best_score = beam[0].score;
            best_seen  = {beam[0]};
            cout << "[PreBeam] Depth " << depth + 1 << ": improved score to " << best_score << "\n";
        }
        else
            cout << "[PreBeam] Depth " << depth + 1 << ": no improvement (best=" << best_score << ")\n";
    }

    CropState &winner = best_seen[0];
    cout << "[PreBeam] Done. Score: " << init_score << " -> " << winner.score
         << " in " << winner.path.size() << " rotations\n";

    vector<Rotation> global_path;
    global_path.reserve(winner.path.size());
    for (auto &rot : winner.path)
        global_path.emplace_back(rot.k, rot.i + cnt, rot.j + cnt);
    return global_path;
}

// ── Do-step beam search (GPU-accelerated) ─────────────────────────────────────
vector<Rotation> Do_step_beam_search(const vector<vector<uint16_t>> &crop,
                                      int Fsize, int PD,
                                      GPUBeamSearch &gpu_search)
{
    const int MAX_DEPTH  = Fsize;
    const int BEAM_WIDTH = 125 + 10 * cnt;
    const int N          = (int)crop.size();

    cout << "[DoBeam] Starting on " << N << "x" << N
         << " crop (cnt=" << cnt << ", Fsize=" << Fsize << ")\n";

    struct CropState {
        vector<vector<uint16_t>> grid;
        vector<Rotation>         path;
        int                      score;
    };

    // Pre-compute valid rotations (k <= min(12, N-1))
    vector<tuple<int,int,int>> valid_rots;
    for (int k = 2; k <= min(12, N - 1); ++k)
        for (int r = 0; r <= N - k; ++r)
            for (int c = 0; c <= N - k; ++c)
                if (Check_Valid(r + cnt, c + cnt, k - 1))
                    valid_rots.emplace_back(k, r, c);

#ifdef USE_CUDA
    const bool use_gpu = (N <= CCB_MAX_N);
    if (!use_gpu)
        cout << "[DoBeam] N=" << N << " > CCB_MAX_N=" << CCB_MAX_N << " — CPU fallback.\n";
#else
    const bool use_gpu = false;
#endif

    auto cpu_score = [&](const vector<vector<uint16_t>> &g) {
        return weighted_beam_DO(g, Fsize, PD);
    };

    int  init_score = cpu_score(crop);
    cout << "[DoBeam] Initial weighted free-pair score: " << init_score << "\n";

    vector<CropState> beam      = {{crop, {}, init_score}};
    vector<CropState> best_seen = beam;
    int               best_score = init_score;
    bool              bk         = false;

    for (int depth = 0; depth < MAX_DEPTH; depth++)
    {
        vector<CropState> next_beam;

#ifdef USE_CUDA
        if (use_gpu)
        {
            const int S = (int)beam.size();
            vector<uint16_t> flat(S * N * N);
            for (int s = 0; s < S; ++s)
                for (int r = 0; r < N; ++r)
                    for (int c = 0; c < N; ++c)
                        flat[s * N * N + r * N + c] = beam[s].grid[r][c];

            // mode 1 = weighted_beam_DO
            auto results = gpu_search.evaluate_crop(flat, S, N, valid_rots,
                                                    cnt, Fsize, PD, /*mode=*/1);
            next_beam.reserve(results.size());
            for (const auto &res : results)
            {
                const auto &[k, r, c] = valid_rots[res.rot_idx];
                CropState ns;
                ns.grid  = rotate_submatrix(beam[res.parent_idx].grid, k, r, c);
                ns.path  = beam[res.parent_idx].path;
                ns.path.emplace_back(k, r, c);
                ns.score = res.score;
                next_beam.push_back(std::move(ns));
                if (ns.score >= 99999) { bk = true; break; }
            }
        }
        else
#endif
        {
#pragma omp parallel
            {
                vector<CropState> local_next;
#pragma omp for schedule(dynamic) nowait
                for (int bi = 0; bi < (int)beam.size(); bi++)
                {
                    const auto &cur = beam[bi];
                    for (int k = 2; k <= min(12, N - 1); k++)
                    {
                        for (int r = 0; r <= N - k; r++)
                        {
                            for (int c = 0; c <= N - k; c++)
                            {
                                if (!Check_Valid(r + cnt, c + cnt, k - 1)) continue;
                                auto new_grid  = rotate_submatrix(cur.grid, k, r, c);
                                int  new_score = cpu_score(new_grid);
                                auto new_path  = cur.path;
                                new_path.emplace_back(k, r, c);
                                local_next.push_back({new_grid, new_path, new_score});
                                if (new_score >= 99999) { bk = true; break; }
                            }
                            if (bk) break;
                        }
                        if (bk) break;
                    }
                    if (bk) break;
                }
#pragma omp critical
                { next_beam.insert(next_beam.end(), local_next.begin(), local_next.end()); }
            }
        }

        if (next_beam.empty()) break;

        sort(next_beam.begin(), next_beam.end(),
             [](const CropState &a, const CropState &b) { return a.score > b.score; });
        if ((int)next_beam.size() > BEAM_WIDTH) next_beam.resize(BEAM_WIDTH);
        beam = std::move(next_beam);

        if (beam[0].score > best_score)
        {
            best_score = beam[0].score;
            best_seen  = {beam[0]};
            cout << "[DoBeam] Depth " << depth + 1 << ": improved score to " << best_score << "\n";
        }
        else
            cout << "[DoBeam] Depth " << depth + 1 << ": no improvement (best=" << best_score << ")\n";
        if (bk) break;
    }

    CropState &winner = best_seen[0];
    cout << "[DoBeam] Done. Score: " << init_score << " -> " << winner.score
         << " in " << winner.path.size() << " rotations\n";

    vector<Rotation> global_path;
    global_path.reserve(winner.path.size());
    for (auto &rot : winner.path)
        global_path.emplace_back(rot.k, rot.i + cnt, rot.j + cnt);
    return global_path;
}

pair<vector<vector<uint16_t>>, vector<Rotation>>
STEP_Do(int Fsize, vector<vector<uint16_t>> grid, int mode, GPUBeamSearch &gpu_search)
{
    int half = Fsize / 2;
    int row  = half - 2;

    cout << "\n=== Beam search prep (Fsize=" << Fsize << ") ===\n";

    // vector<Rotation> pre_path = pre_step_beam_search(grid, Fsize, mode, gpu_search);
    // if (!pre_path.empty())
    // {
    //     vector<Rotation> local_path;
    //     local_path.reserve(pre_path.size());
    //     for (const auto &rot : pre_path)
    //         local_path.emplace_back(rot.k, rot.i - cnt, rot.j - cnt);
    //     grid = apply_rotations(grid, local_path);
    //     cout << "Applied " << pre_path.size() << " rotations\n";
    // }
    // cout << "==========================================\n\n";
    vector<Rotation> pre_path;
    // Sync locked state to GPU before Do_step (pre_step may have changed it)
#ifdef USE_CUDA
    gpu_search.update_locked(locked, n);
#endif

    vector<Rotation> Do_path = Do_step_beam_search(grid, Fsize, mode, gpu_search);
    if (!Do_path.empty())
    {
        vector<Rotation> local_path;
        local_path.reserve(Do_path.size());
        for (const auto &rot : Do_path)
            local_path.emplace_back(rot.k, rot.i - cnt, rot.j - cnt);
        grid = apply_rotations(grid, local_path);
        cout << "Applied " << Do_path.size() << " rotations\n";
    }

    for (int j = 0; j < half; j++)
    {
        locked[cnt + row][cnt + j]     = 1;
        locked[cnt + row + 1][cnt + j] = 1;
    }
    pre_path.insert(pre_path.end(), Do_path.begin(), Do_path.end());
    locked = rotate_submatrix_u8(locked, half, cnt, cnt);
    grid   = rotate_submatrix(grid, half, 0, 0);
    pre_path.emplace_back(half, cnt, cnt);
    cout << "Rotate half\n";

    return {grid, pre_path};
}

// ── Beam Search (GPU-accelerated) ──

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

vector<GridState> unstuck_healing(const GridState &stuck_state, int num_random_moves)
{
    cout << "HEALING: Applying combination of random moves and 3-cycle macro moves..." << endl;
    vector<GridState> healed_states;
    random_device rd;
    mt19937 gen(rd());

    for (int attempt = 0; attempt < 15; attempt++)
    {
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
            int num_macro_cycles  = num_random_moves / 2;
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

        int new_paired    = count_paired_values(current_grid);
        int new_heuristic = calculate_manhattan_heuristic(current_grid);
        uint64_t new_hash = compute_zobrist_hash(current_grid);
        healed_states.push_back({current_grid, current_path, new_paired, new_heuristic, new_hash});
    }

    sort(healed_states.begin(), healed_states.end(), [](const GridState &a, const GridState &b)
         {
             if (a.paired_count != b.paired_count) return a.paired_count > b.paired_count;
             return a.heuristic < b.heuristic;
         });

    cout << "HEALING: Best healed state has " << healed_states[0].paired_count << " pairs" << endl;
    return healed_states;
}

vector<Rotation> beam_search(const vector<vector<uint16_t>> &inner_grid, int offset,
                              int max_depth, GPUBeamSearch &gpu_search)
{
    int inner_n       = inner_grid.size();
    int target_paired = inner_n * inner_n / 2;

    int initial_paired    = count_paired_values(inner_grid);
    int initial_heuristic = calculate_manhattan_heuristic(inner_grid);
    uint64_t initial_hash = compute_zobrist_hash(inner_grid);

    int    base_beam_width = 4000;
    double beam_multiplier = 1.0;

    gpu_search.set_zobrist_table(&zobrist_table[0][0][0]);

    int num_threads = omp_get_max_threads();
    cout << "Starting Beam Search on " << inner_n << "x" << inner_n
         << " inner block (offset=" << offset << ", beam=" << base_beam_width
         << ", threads=" << num_threads << ", GPU processing enabled)\n";
    cout << "Initial paired: " << initial_paired << "/" << target_paired << "\n";

    // Pre-build valid rotations list
    vector<tuple<int, int, int>> valid_rotations;
    for (int k = 2; k <= inner_n - 1; ++k)
        for (int i = 0; i <= inner_n - k; ++i)
            for (int j = 0; j <= inner_n - k; ++j)
                if (Check_Valid(i + offset, j + offset, k - 1))
                    valid_rotations.push_back({k, i, j});

    priority_queue<GridState> beam;
    beam.push({inner_grid, {}, initial_paired, initial_heuristic, initial_hash});

    GridState global_best = {inner_grid, {}, initial_paired, initial_heuristic, initial_hash};
    int depth = 0;
    int stuck_counter    = 0;
    int last_best_paired = initial_paired;

    vector<StateSnapshot> state_history;
    unordered_map<int, int> paired_count_visits;

    unordered_set<uint64_t> global_visited;
    global_visited.insert(initial_hash);

    for (; depth < max_depth && !beam.empty(); ++depth)
    {
        if (is_time_up())
        {
            cout << "Time limit reached at depth " << depth << endl;
            for (auto &r : global_best.path) { r.i += offset; r.j += offset; }
            return global_best.path;
        }

        vector<GridState> current_states;
        while (!beam.empty())
        {
            current_states.push_back(beam.top());
            beam.pop();
        }

        int num_states = current_states.size();
        vector<uint16_t> grids_flat(num_states * inner_n * inner_n);
        for (int s = 0; s < num_states; ++s)
            for (int r = 0; r < inner_n; ++r)
                for (int c = 0; c < inner_n; ++c)
                    grids_flat[s * inner_n * inner_n + r * inner_n + c] = current_states[s].grid[r][c];

        vector<GPUResult> gpu_results = gpu_search.evaluate_batch(
            grids_flat, num_states, valid_rotations, inner_n);

        priority_queue<GridState> next_beam;
        int  best_paired_in_depth = 0;
        bool solution_found       = false;
        vector<Rotation> solution_path;

        for (const auto &res : gpu_results)
        {
            if (res.paired_count < current_states[res.parent_idx].paired_count - 2)
                continue;

            if (global_visited.find(res.hash) == global_visited.end())
            {
                global_visited.insert(res.hash);

                GridState ns = current_states[res.parent_idx];
                int k = get<0>(valid_rotations[res.rotation_idx]);
                int i = get<1>(valid_rotations[res.rotation_idx]);
                int j = get<2>(valid_rotations[res.rotation_idx]);

                ns.grid = rotate_submatrix(ns.grid, k, i, j);
                ns.path.emplace_back(k, i, j);
                ns.paired_count = res.paired_count;
                ns.heuristic    = res.heuristic;
                ns.hash         = res.hash;

                if (ns.paired_count > best_paired_in_depth)
                    best_paired_in_depth = ns.paired_count;
                if (ns.paired_count > global_best.paired_count)
                    global_best = ns;

                if (is_solved(ns.grid))
                {
                    solution_found = true;
                    solution_path  = ns.path;
                    break;
                }
                next_beam.push(std::move(ns));
            }
        }

        if (solution_found)
        {
            cout << "Solution found at depth " << depth << endl;
            for (auto &r : solution_path) { r.i += offset; r.j += offset; }
            return solution_path;
        }

        cout << "Depth " << depth << ": " << current_states.size()
             << " states, best paired: " << best_paired_in_depth << "/" << target_paired;

        int gpu_best_paired = best_paired_in_depth;

        if (best_paired_in_depth > last_best_paired)
        {
            cout << " (IMPROVED!)";
            StateSnapshot snapshot;
            snapshot.beam_states       = current_states;
            snapshot.paired_count      = best_paired_in_depth;
            snapshot.depth_at_snapshot = depth;
            snapshot.beam_multiplier   = beam_multiplier;
            state_history.push_back(snapshot);
            last_best_paired = best_paired_in_depth;
            stuck_counter    = 0;
        }
        else
        {
            stuck_counter++;
            cout << " (stuck: " << stuck_counter << "/6)";
        }
        cout << "\n";

        // Macro cycle injection when mildly stuck
        if (stuck_counter >= 4 && !solution_found)
        {
            for (int idx = 0; idx < num_states; ++idx)
            {
                GridState current = current_states[idx];
                for (int k = 2; k < inner_n - 1; ++k)
                    for (int i = 0; i <= inner_n - k; ++i)
                        for (int j = 0; j <= inner_n - k - 1; ++j)
                        {
                            if (Check_Valid(i + offset, j + offset, k - 1))
                            {
                                vector<Rotation> macro_path = current.path;
                                vector<vector<uint16_t>> macro_grid =
                                    apply_macro_cycle(current.grid, k, i, j, macro_path);
                                int macro_paired = count_paired_values(macro_grid);
                                if (macro_paired >= current.paired_count)
                                {
                                    int macro_heuristic = calculate_manhattan_heuristic(macro_grid);
                                    uint64_t mk = compute_zobrist_hash(macro_grid);
                                    if (global_visited.find(mk) == global_visited.end())
                                    {
                                        global_visited.insert(mk);
                                        if (macro_paired > global_best.paired_count)
                                            global_best = {macro_grid, macro_path, macro_paired, macro_heuristic, mk};
                                        if (macro_paired > best_paired_in_depth)
                                            best_paired_in_depth = macro_paired;
                                        if (is_solved(macro_grid))
                                        {
                                            solution_found = true;
                                            solution_path  = macro_path;
                                            break;
                                        }
                                        next_beam.push({macro_grid, macro_path, macro_paired, macro_heuristic, mk});
                                    }
                                }
                            }
                        }
            }
        }

        // Backtracking / healing when deeply stuck
        if (stuck_counter >= 6)
        {
            if (!state_history.empty())
            {
                cout << "\n!!! BACKTRACKING TRIGGERED !!!" << endl;
                bool          found_valid_backtrack = false;
                StateSnapshot backtrack_target;

                while (!state_history.empty())
                {
                    StateSnapshot candidate = state_history.back();
                    state_history.pop_back();
                    if (candidate.paired_count < best_paired_in_depth)
                    {
                        if (paired_count_visits[candidate.paired_count] < 2)
                        {
                            backtrack_target      = candidate;
                            found_valid_backtrack = true;
                            paired_count_visits[candidate.paired_count]++;
                            cout << "Backtrack target: " << backtrack_target.paired_count << " pairs\n";
                            break;
                        }
                    }
                }

                if (found_valid_backtrack)
                {
                    beam_multiplier = backtrack_target.beam_multiplier * 1.5;
                    next_beam       = priority_queue<GridState>();
                    for (const auto &state : backtrack_target.beam_states)
                        next_beam.push(state);
                    backtrack_target.beam_multiplier = beam_multiplier;
                    state_history.push_back(backtrack_target);
                    stuck_counter        = 0;
                    last_best_paired     = backtrack_target.paired_count;
                    best_paired_in_depth = backtrack_target.paired_count;
                    cout << "Backtracking done. Beam multiplier: " << beam_multiplier << "\n";
                }
                else
                {
                    cout << "No valid backtrack. Healing...\n";
                    GridState best_cur = current_states[0];
                    for (const auto &s : current_states)
                        if (s.paired_count > best_cur.paired_count) best_cur = s;
                    auto healed     = unstuck_healing(best_cur, min(10, inner_n / 2));
                    int  best_healed = 0;
                    for (const auto &hs : healed)
                    {
                        next_beam.push(hs);
                        best_healed = max(best_healed, hs.paired_count);
                    }
                    if (best_healed > 0)
                    {
                        best_paired_in_depth = best_healed;
                        last_best_paired     = best_healed;
                    }
                    stuck_counter = 0;
                }
            }
            else
            {
                cout << "STUCK — no history. Healing...\n";
                GridState best_cur = current_states[0];
                for (const auto &s : current_states)
                    if (s.paired_count > best_cur.paired_count) best_cur = s;
                auto healed     = unstuck_healing(best_cur, min(10, inner_n / 2));
                int  best_healed = 0;
                for (const auto &hs : healed)
                {
                    next_beam.push(hs);
                    best_healed = max(best_healed, hs.paired_count);
                }
                if (best_healed > 0)
                {
                    best_paired_in_depth = best_healed;
                    last_best_paired     = best_healed;
                }
                stuck_counter = 0;
            }
        }

        // Trim beam
        int dbw = base_beam_width;
        if (depth < 10) dbw -= 2000;
        int current_beam_width = (int)(dbw * beam_multiplier);
        beam = priority_queue<GridState>();
        int kept = 0;
        while (!next_beam.empty() && kept < current_beam_width)
        {
            GridState state = next_beam.top();
            next_beam.pop();
            if (state.paired_count >= gpu_best_paired - 1)
            {
                beam.push(state);
                kept++;
            }
        }

        if (kept == 0)
        {
            cout << "No improving moves. Search terminated.\n";
            break;
        }
        cout << "Kept " << kept << " states (beam width: " << current_beam_width << ")\n";
    }

    cout << "Beam search done. Best paired: " << global_best.paired_count << "/" << target_paired << "\n";
    for (auto &r : global_best.path) { r.i += offset; r.j += offset; }
    return global_best.path;
}

int main()
{
    init_zobrist();
    omp_set_num_threads(omp_get_max_threads());
    cout << "Using " << omp_get_max_threads() << " threads\n";

    int on_comp = 0;
    const string SERVER_URL = (on_comp == 1) ? "http://10.0.0.1:3000" : "http://localhost:3000";
    const string TOKEN      = "player1";

    vector<vector<uint16_t>> init_grid;

    if (on_comp == 1 || TEST_WITH_NAPROCK_REAL_SERVER)
    {
        cout << "Using NAPROCK SERVER\n";
        cout << "Checkpoint 1: Starting Connection..." << endl;
        json match_json = fetch_match_data(SERVER_URL, TOKEN);

        cout << "Checkpoint 2: Data received!" << endl;
        if (!match_json.contains("problem"))
        {
            cout << "Checkpoint 3: No problem field found! Exiting." << endl;
            return 0;
        }

        cout << "Checkpoint 4: Problem found, starting solver..." << endl;
        auto &field_obj = match_json["problem"]["field"];
        n = field_obj["size"].get<int>();
        cout << "Detected N = " << n << endl;

        auto &entities = field_obj["entities"];
        init_grid.assign(n, vector<uint16_t>(n));
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                init_grid[i][j] = entities[i][j].get<uint16_t>();

        cout << "GET SUCCESS (NAPROCK SERVER). Grid is ready!" << endl;
    }
    else
    {
        cout << "Use Pun's Server\n";
        init_grid = get_random_board(24);
        n = init_grid.size();
    }

    vector<vector<uint16_t>> grid = init_grid;
    n      = grid.size();
    locked = vector<vector<uint8_t>>(n, vector<uint8_t>(n, 0));

    vector<Rotation> full_path;

    // Single gpu_search instance shared across Phase 1 and Phase 2
    GPUBeamSearch gpu_search(n, 30000, 5000);
    gpu_search.set_zobrist_table(&zobrist_table[0][0][0]);
#ifdef USE_CUDA
    gpu_search.update_locked(locked, n);   // initial: all zeros
#endif

    // ── Phase 1: Frame-by-frame STEP_Do solver ──
    for (int Fsize = n - cnt * 2; Fsize > 10; Fsize -= 4)
    {
        vector<vector<uint16_t>> crop;
        vector<Rotation> partial_path;
        crop.reserve(n - cnt * 2);

        for (int i = cnt; i < n - cnt; i++)
        {
            vector<uint16_t> row(grid[i].begin() + cnt, grid[i].end() - cnt);
            crop.push_back(row);
        }

        pair<vector<vector<uint16_t>>, vector<Rotation>> res =
            STEP_Do(Fsize, crop, 0, gpu_search);
        partial_path.insert(partial_path.end(), res.second.begin(), res.second.end());

        crop   = rotate_submatrix(res.first, Fsize, 0, 0);
        locked = rotate_submatrix_u8(locked, Fsize, cnt, cnt);
        partial_path.emplace_back(Fsize, cnt, cnt);

        for (int i = 0; i < 3; i++)
        {
            auto res1 = STEP_Do(Fsize, crop, 0, gpu_search);
            partial_path.insert(partial_path.end(), res1.second.begin(), res1.second.end());

            auto res2 = STEP_Do(Fsize, res1.first, 2, gpu_search);
            partial_path.insert(partial_path.end(), res2.second.begin(), res2.second.end());

            crop   = rotate_submatrix(res2.first, Fsize, 0, 0);
            locked = rotate_submatrix_u8(locked, Fsize, cnt, cnt);
            partial_path.emplace_back(Fsize, cnt, cnt);

#ifdef USE_CUDA
            gpu_search.update_locked(locked, n);   // sync after each frame rotation
#endif
        }

        res = STEP_Do(Fsize, crop, 2, gpu_search);
        partial_path.insert(partial_path.end(), res.second.begin(), res.second.end());

        grid = apply_rotations(grid, partial_path);
        full_path.insert(full_path.end(), partial_path.begin(), partial_path.end());

        cout << "Current Frame cost :"       << partial_path.size() << '\n';
        cout << "Current Frame total cost :" << full_path.size()    << '\n';
        cout << "------------------------------------\n";
        cout << "Locked: \n";    print_grid(locked);
        cout << "crop (cnt = " << cnt << " ):\n"; print_grid(res.first);
        cout << "grid: \n";      print_grid(grid);
        cout << "------------------------------------\n";

        cnt += 2;
    }

    // ── Phase 2: GPU Beam Search on remaining inner block ──
    {
        int inner_n = n - cnt * 2;
        int offset  = cnt;

        vector<vector<uint16_t>> inner_grid;
        inner_grid.reserve(inner_n);
        for (int i = offset; i < offset + inner_n; i++)
        {
            vector<uint16_t> row(grid[i].begin() + offset, grid[i].begin() + offset + inner_n);
            inner_grid.push_back(row);
        }

        int inner_paired = count_paired_values(inner_grid);
        int inner_target = inner_n * inner_n / 2;

        cout << "\n=== Beam Search on " << inner_n << "x" << inner_n
             << " inner block (global offset=" << offset << ") ===\n";
        cout << "Inner pairs solved: " << inner_paired << "/" << inner_target << "\n";

        if (inner_paired < inner_target)
        {
            int max_search_depth    = inner_n * inner_n * 2;
            vector<Rotation> beam_path = beam_search(inner_grid, offset, max_search_depth, gpu_search);

            grid = apply_rotations(grid, beam_path);
            full_path.insert(full_path.end(), beam_path.begin(), beam_path.end());

            vector<vector<uint16_t>> inner_after;
            for (int i = offset; i < offset + inner_n; i++)
            {
                vector<uint16_t> r(grid[i].begin() + offset, grid[i].begin() + offset + inner_n);
                inner_after.push_back(r);
            }
            cout << "After beam search inner pairs: "
                 << count_paired_values(inner_after) << "/" << inner_target << "\n";
        }
        else
            cout << "Inner block already fully solved — beam search skipped.\n";
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
    auto end_time = high_resolution_clock::now();
    duration<double> elapsed = end_time - start_time;
    cout << "Total time used: " << elapsed.count() << " seconds\n";

    if (TEST_WITH_NAPROCK_REAL_SERVER)
        submit_answer(SERVER_URL, TOKEN, full_path);

    if (broke)
        cout << "-------------------------------------BROKE----------------------------------\n";

    return 0;
}