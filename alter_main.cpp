#include <bits/stdc++.h>
#include <fstream>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <omp.h>
#include <random>

using namespace std;
using namespace std::chrono;
using json = nlohmann::json;

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



int weighted_free_pairs(const vector<vector<uint16_t>> &crop, int Fsize, int local_cnt)
{
    // 7-tier priority scoring for free pairs based on position:
    // Weight 10: SQUARE BOX - four cells forming a 2x2 square with matching values
    // Weight 7: middle rows (n/2-1, n/2) - STEP_Do's target rows
    // Weight 6: lower rows + center columns
    // Weight 5: lower rows + right columns
    // Weight 4: lower rows + left columns
    // Weight 3: upper rows + center columns
    // Weight 2: upper rows + right columns
    // Weight 1: upper rows + left columns

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
                return 5;

            // Lower rows
            if (is_lower)
            {
                if (is_center)
                    return 3; // Priority 2
                if (is_right)
                    return 1; // Priority 3
                if (is_left)
                    return 3; // Priority 4
            }

            // Upper rows
            if (is_upper)
            {
                if (mid_row_start - 1 == row)
                {
                    return 3;
                }
                else
                {
                    return 1;
                }
            }
        }
        else
        {

            // Horizontal

            if (row == mid_row_end)
                return 7;
            // Lower rows
            if (is_lower)
            {
                if (is_center)
                    return 8; // Priority 2
                if (is_right)
                    return 4; // Priority 3
                if (is_left)
                    return 8; // Priority 4
            }

            // Upper rows
            if (is_upper)
            {
                return 1; // Priority 7
            }
        }

        // Default (should not reach here)
        return 1;
    };

    set<int> sr;
    // First pass: check if there are same number in given area (weight 10)
    for (int i = mid_row_start; i < N - 1; i++)
    {
        for (int j = local_cnt; j < min(N - 1, local_cnt + Fsize / 4); j++)
        {
            if (locked[cnt + i][cnt + j])
                continue;
            if (auto search = sr.find(crop[i][j]); search != sr.end())
            {
                pair<int, int> pos = find_pos(crop, i, j);
                if (abs(pos.first - i) <= 1 || abs(pos.second - j) <= 1)
                    score += 2;
            }
            else
            {
                sr.insert(crop[i][j]);
            }
        }
    }

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
            if (locked[cnt + i][cnt + j])
                continue;

            // Vertical pair: (i,j)-(i+1,j)
            if (i + 1 < N && !locked[cnt + i + 1][cnt + j] && crop[i][j] == crop[i + 1][j])
            {
                int w1 = get_weight(i, j, 1);

                score += w1;
            }
        }
    }
    return score;
}

int weighted_beam_DO(const vector<vector<uint16_t>> &crop, int Fsize)
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
        bool is_center = (col >= half - 2  && col <= half + 1 );
        bool is_right = (col >= half);
        bool is_left = (col < half );

        if (mode)
        {
            // Priority 1 : middle rows
            if (row == mid_row_start)
                return 5;

            // Lower rows
            if (is_lower)
            {
                if (is_center)
                    return 3; // Priority 2
                if (is_right)
                    return 1; // Priority 3
                if (is_left)
                    return 3; // Priority 4
            }

            // Upper rows
            if (is_upper)
            {
                if (mid_row_start - 1 == row)
                {
                    return 3;
                }
                else
                {
                    return 1;
                }
            }
        }
        else
        {

            // Horizontal

            if (row == mid_row_end)
                return 7;
            // Lower rows
            if (is_lower)
            {
                if (is_center)
                    return 8; // Priority 2
                if (is_right)
                    return 4; // Priority 3
                if (is_left)
                    return 8; // Priority 4
            }

            // Upper rows
            if (is_upper)
            {
                return 1; // Priority 7
            }
        }

        // Default (should not reach here)
        return 1;
    };

    int CUR_J = 0;
    for (int j = 0; j <half;)
    {
        if (crop[mid_row_start][j] == crop[mid_row_end][j])
        {
            j++;
            score+=1000;
        }
        else if (((crop[mid_row_start][j] == crop[mid_row_start][j + 1]) && (crop[mid_row_end][j] == crop[mid_row_end][j + 1]))&&(j!=half-1))
        {
            j+=2;
            score += 2000;
        }
        else
        {
            break;
        }
        CUR_J = j;
    }
    if(CUR_J >= half)return 99999;
    set<int> sr;
    // First pass: check if there are same number in given area (weight 10)
    for (int i = mid_row_start; i < N - 1; i++)
    {
        for (int j = CUR_J; j < min(N - 1, CUR_J + Fsize / 4); j++)
        {
            if (locked[cnt + i][cnt + j])
                continue;
            if (auto search = sr.find(crop[i][j]); search != sr.end())
            {
                pair<int, int> pos = find_pos(crop, i, j);
                if (abs(pos.first - i) <= 1 || abs(pos.second - j) <= 1)
                    score += 2;
            }
            else
            {
                sr.insert(crop[i][j]);
            }
        }
    }

    // Second pass: count regular pairs with positional weights
    for (int j = CUR_J; j < N; j++)
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
                score += w;
                break;
            }
        }
    }
    for (int i = mid_row_start - 1; i < N; i++)
    {
        for (int j = CUR_J; j < N; j++)
        {
            // Skip locked cells (translate to global coords using global cnt)
            if (locked[cnt + i][cnt + j])
                continue;

            // Vertical pair: (i,j)-(i+1,j)
            if (i + 1 < N && !locked[cnt + i + 1][cnt + j] && crop[i][j] == crop[i + 1][j])
            {
                int w1 = get_weight(i, j, 1);

                score += w1;
            }
        }
    }
    return score;
}

// Returns rotations in GLOBAL coordinates (i += cnt, j += cnt already applied).
vector<Rotation> pre_step_beam_search(const vector<vector<uint16_t>> &crop, int Fsize, int PD = 0)
{
    const int MAX_DEPTH = 2;
    const int BEAM_WIDTH = 100;
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

vector<Rotation> Do_step_beam_search(const vector<vector<uint16_t>> &crop, int Fsize)
{
    const int MAX_DEPTH = Fsize;
    const int BEAM_WIDTH = 100;
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
        return weighted_beam_DO(g, Fsize);
    };

    int init_score = make_score(crop);
    cout << "[PreBeam] Initial weighted free-pair score: " << init_score << "\n";

    vector<CropState> beam = {{crop, {}, init_score}};
    vector<CropState> best_seen = beam;
    int best_score = init_score;
    bool bk = false;
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

                for (int k = 2; k <= min(12,N-1); k++)
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
                            if(new_score >= 99999)
                            {
                                bk = true;
                                break;
                            }
                        }
                        if(bk)break;
                    }
                    if (bk)
                        break;
                }
                if (bk)
                    break;
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
        if(bk)break;
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
    vector<Rotation> Do_path = Do_step_beam_search(grid, Fsize);
    if (!Do_path.empty())
    {
        // Apply to crop in LOCAL coords
        vector<Rotation> local_path;
        local_path.reserve(Do_path.size());
        for (const auto &rot : Do_path)
            local_path.emplace_back(rot.k, rot.i - cnt, rot.j - cnt);
        grid = apply_rotations(grid, local_path);

        // Record in partial_path with GLOBAL coords
        cout << "Applied " << Do_path.size() << " rotations\n";
    }
    for (int j = 0; j < half;j++)
    {
       locked[cnt+row][cnt+j]= 1;
       locked[cnt + row+1][cnt+j] = 1;
    }
    pre_path.insert(pre_path.end(),Do_path.begin(),Do_path.end());
    locked = rotate_submatrix_u8(locked, half, cnt, cnt);
    grid = rotate_submatrix(grid, half, 0, 0);
    pre_path.emplace_back(half, cnt, cnt);
    cout << "Rotate half\n";

    return {grid, pre_path};
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

    int base_beam_width = 2000;
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
                for (int k = 2; k <= min(8, inner_n); ++k)
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
            cout << " (stuck: " << stuck_counter << "/5)";
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

        if (stuck_counter >= 5)
        {
            if (!state_history.empty())
            {
                cout << "\n!!! BACKTRACKING TRIGGERED !!!" << endl;
                bool found_valid_backtrack = false;
                StateSnapshot backtrack_target;

                while (!state_history.empty())
                {
                    StateSnapshot candidate = state_history.back();
                    state_history.pop_back();
                    if (candidate.paired_count < best_paired_in_depth)
                    {
                        int visit_count = paired_count_visits[candidate.paired_count];
                        if (visit_count < 2)
                        {
                            backtrack_target = candidate;
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
                    next_beam = priority_queue<GridState>();
                    for (const auto &state : backtrack_target.beam_states)
                        next_beam.push(state);
                    backtrack_target.beam_multiplier = beam_multiplier;
                    state_history.push_back(backtrack_target);
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
        int dbw = base_beam_width;
        if (depth < 35)
        {
            dbw -= 1500;
        }
        int current_beam_width = (int)(dbw * beam_multiplier);
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
    auto end_time = high_resolution_clock::now();
    duration<double> elapsed = end_time - start_time;
    cout << "Total time used: " << elapsed.count() << " seconds\n";

    if (broke)
        cout << "-------------------------------------BROKE----------------------------------\n";

    return 0;
}