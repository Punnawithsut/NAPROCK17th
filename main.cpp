#include <bits/stdc++.h>
#include <fstream>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <omp.h>

using namespace std;
using json = nlohmann::json;

struct Rotation
{
    int k, i, j;
    Rotation(int k, int i, int j) : k(k), i(i), j(j) {}
};

struct ExtendedPair {
    tuple<int, int, int, int> position; //since it is square only collect top left row, top left col, buttom right row, bottom right col.
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
    {
        for (int j = 0; j < n; ++j)
        {
            board[i][j] = static_cast<uint16_t>(responseData["board"][i][j].get<int>());
        }
    }

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
        {
            row_json.push_back(static_cast<int>(val));
        }
        board_json.push_back(row_json);
    }

    result_file["initialBoard"] = board_json;
    json rotations_map = json::object();

    for (int k = 0; k < fp.size(); k++)
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

int count_adjacent_pairs(const vector<vector<uint16_t>> &grid)
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

void print_grid(const vector<vector<uint16_t>> &grid)
{
    for (const auto &row : grid)
    {
        for (uint16_t val : row)
        {
            cout << setw(3) << val << " ";
        }
        cout << endl;
    }
}

void print_grid(const vector<vector<uint8_t>> &grid)
{
    for (const auto &row : grid)
    {
        for (uint8_t val : row)
        {
            cout << setw(3) << static_cast<int>(val) << " ";
        }
        cout << endl;
    }
}

vector<vector<uint16_t>> apply_rotations(vector<vector<uint16_t>> grid, const vector<Rotation> &path)
{
    for (const auto &rot : path)
    {
        grid = rotate_submatrix(grid, rot.k, rot.i, rot.j);
    }
    return grid;
}

bool Check_Valid(int i, int j, int k)
{
    if (locked[i][j] || locked[i + k][j + k] || locked[i][j + k] || locked[i + k][j])
        return false;
    for (int ft = 1; ft < k; ft += 1)
    {
        if (locked[i + ft][j] || locked[i][j + ft] || locked[i + ft][j + k] || locked[i + k][j + ft])
            return false;
    }
    return true;
}

string serialize(const vector<vector<uint16_t>> &g)
{
    string s;
    int N = g.size();
    for (int r = 0; r < N; r++)
    {
        for (int c = 0; c < N; c++)
        {
            s += to_string(g[r][c]) + ",";
        }
    }
    return s;
}

struct State
{
    vector<vector<uint16_t>> grid;
    vector<Rotation> path;
    pair<int, int> pos;
};

pair<int, int> find_pos(const vector<vector<uint16_t>> &grid, int row, int col)
{
    if (grid.empty() || row < 0 || col < 0 || row >= grid.size() || col >= grid[0].size())
        return make_pair(-1, -1);

    uint16_t target = grid[row][col];
    for (int i = 0; i < grid.size(); ++i)
    {
        for (int j = 0; j < grid[i].size(); ++j)
        {
            if ((i == row && j == col))
                continue;
            if (grid[i][j] == target)
                return make_pair(i, j);
        }
    }
    return make_pair(-1, -1);
}

vector<tuple<int, int, int, int, uint16_t>> find_free_pairs(const vector<vector<uint16_t>> &grid, int track)
{
    vector<tuple<int, int, int, int, uint16_t>> pairs;
    int N = grid.size();

    for (int i = 0; i < N; ++i)
    {
        for (int j = track; j < N; ++j)
        {
            if (locked[cnt + i][cnt + j])
                continue;

            if (j + 1 < N && !locked[cnt + i][cnt + j + 1] && grid[i][j] == grid[i][j + 1])
            {
                pairs.push_back({i, j, i, j + 1, grid[i][j]});
            }
            if (i + 1 < N && !locked[cnt + i + 1][cnt + j] && grid[i][j] == grid[i + 1][j])
            {
                pairs.push_back({i, j, i + 1, j, grid[i][j]});
            }
        }
    }
    return pairs;
}

pair<int, vector<Rotation>> move_free_pair_to_target(const vector<vector<uint16_t>> &initial_grid,
                                                     int pr1, int pc1, int pr2, int pc2,
                                                     int target_r, int target_c, bool is_vertical)
{
    // TODO: If pair is horizontally placed then find adjacent (only up and down) pair and move it together
    const int beam_width = 30;
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
            {
                at_target = (cur.grid[target_r][target_c] == cur.grid[target_r + 1][target_c] &&
                             cur.grid[target_r][target_c] == initial_grid[pr1][pc1]);
            }
            else
            {
                at_target = (cur.grid[target_r][target_c] == cur.grid[target_r][target_c + 1] &&
                             cur.grid[target_r][target_c] == initial_grid[pr1][pc1]);
            }

            if (at_target)
            {
                return {static_cast<int>(cur.path.size()), cur.path};
            }
        }

#pragma omp parallel
        {
            vector<State> local_beam;

#pragma omp for schedule(dynamic) nowait
            for (int beam_idx = 0; beam_idx < current_beam.size(); ++beam_idx)
            {
                const auto &cur = current_beam[beam_idx];

                for (int k = 2; k <= N - 1; ++k)
                {
                    for (int r = 0; r <= N - k; ++r)
                    {
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
                }
            }

#pragma omp critical
            {
                next_beam.insert(next_beam.end(), local_beam.begin(), local_beam.end());
            }
        }

        sort(next_beam.begin(), next_beam.end(),
             [&](const State &a, const State &b)
             {
                 int dist_a = abs(a.pos.first - target_r) + abs(a.pos.second - target_c) + 2 * a.path.size();
                 int dist_b = abs(b.pos.first - target_r) + abs(b.pos.second - target_c) + 2 * b.path.size();
                 return dist_a < dist_b;
             });

        if (next_beam.size() > static_cast<size_t>(beam_width))
        {
            next_beam.resize(beam_width);
        }
        current_beam = std::move(next_beam);
        if (current_beam.empty())
            break;
    }

    return {999, {}};
}

pair<int, vector<Rotation>> search_pair(const vector<vector<uint16_t>> &initial_grid, int row1, int col1, int row2, int col2,
                                        int mnr, int mxr, int mnc, int mxc)
{
    const int beam_width = 30 + cnt * 5;
    const int max_depth = 5;
    int N = initial_grid.size();

    vector<State> current_beam = {{initial_grid, {}, find_pos(initial_grid, row1, col1)}};
    if (current_beam[0].pos.first == -1)
    {
        //cout << row1 << ' ' << col1 << " Broke\n";
        broke = true;
        return {1000, {}};
    }

    for (int depth = 0; depth < max_depth; ++depth)
    {
        vector<State> next_beam;

        for (const auto &cur : current_beam)
        {
            if (cur.grid[row1][col1] == cur.grid[row2][col2])
            {
                return {static_cast<int>(cur.path.size()), cur.path};
            }
        }

#pragma omp parallel
        {
            vector<State> local_beam;

#pragma omp for schedule(dynamic) nowait
            for (int beam_idx = 0; beam_idx < current_beam.size(); ++beam_idx)
            {
                const auto &cur = current_beam[beam_idx];

                for (int k = min(max(abs(cur.pos.first - row2),
                                     abs(cur.pos.second - col2)) +
                                     2,
                                 24);
                     k >= 2; --k)
                {
                    for (int r = mnr; r <= mxr + 1 - k; ++r)
                    {
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
                }
            }

#pragma omp critical
            {
                next_beam.insert(next_beam.end(), local_beam.begin(), local_beam.end());
            }
        }

        sort(next_beam.begin(), next_beam.end(),
             [&](const State &a, const State &b)
             {
                 int dist_a = abs(a.pos.first - row2) + abs(a.pos.second - col2) + 2 * a.path.size();
                 int dist_b = abs(b.pos.first - row2) + abs(b.pos.second - col2) + 2 * b.path.size();
                 return dist_a < dist_b;
             });
        if (next_beam.size() > static_cast<size_t>(beam_width))
        {
            next_beam.resize(beam_width);
        }
        current_beam = std::move(next_beam);
        if (current_beam.empty())
            break;
    }

    return {1000, {}};
}

ExtendedPair extend_free_pair(const vector<vector<uint16_t>> &initial_grid, 
                                                int pr1, int pr2, int pc1, int pc2,
                                                bool tl, bool bl, bool tr, bool br, bool is_vertical) {
    if(DEBUG && false) {
        cout << "Extending FREE PAIR!" << endl;
        cout << "pr1=" << pr1 << ", pr2=" << pr2 << ", pc1=" << pc1 << ", pc2=" << pc2 << endl;
        cout << "is_vertical=" << is_vertical << ", tl=" << tl << ", bl=" << bl << ", tr=" << tr << ", br=" << br << endl;
    }
    
    pair<int, vector<Rotation>> temp;
    struct ExtendedPair result;
    result.cost = 1000;
    int N = initial_grid.size();
    
    if(is_vertical) {
        if(tl && pc1 - 1 >= 0) {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr2, pc1 - 1, pr1, pc1 - 1, 
                0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if(temp.first < result.cost) {
                result.cost = temp.first;
                result.position = {pr1, pc1 - 1, pr2, pc1};
                result.path = temp.second;
            }
        }
        if(bl && pc1 - 1 >= 0) {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr1, pc1 - 1, pr2, pc1 - 1, 
                0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if(temp.first < result.cost) {
                result.cost = temp.first;
                result.position = {pr1, pc1 - 1, pr2, pc1};
                result.path = temp.second;
            }
        }
        if(tr && pc1 + 1 < N) {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr2, pc1 + 1, pr1, pc1 + 1, 
                0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if(temp.first < result.cost) {
                result.cost = temp.first;
                result.position = {pr1, pc1, pr2, pc1 + 1};
                result.path = temp.second;
            }
        }
        if(br && pc1 + 1 < N) {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr1, pc1 + 1, pr2, pc1 + 1, 
                0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if(temp.first < result.cost) {
                result.cost = temp.first;
                result.position = {pr1, pc1, pr2, pc1 + 1};
                result.path = temp.second;
            }
        }
    } else {
        if(tl && pr1 - 1 >= 0) {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr1 - 1, pc2, pr1 - 1, pc1, 
                0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if(temp.first < result.cost) {
                result.cost = temp.first;
                result.position = {pr1 - 1, pc1, pr1, pc2};
                result.path = temp.second;
            }
        }
        if(bl && pr1 + 1 < N) {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr1 + 1, pc2, pr1 + 1, pc1, 
                0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if(temp.first < result.cost) {
                result.cost = temp.first;
                result.position = {pr1, pc1, pr1 + 1, pc2};
                result.path = temp.second;
            }
        }
        if(tr && pr1 - 1 >= 0) {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr1 - 1, pc1, pr1 - 1, pc2, 
                0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if(temp.first < result.cost) {
                result.cost = temp.first;
                result.position = {pr1 - 1, pc1, pr1, pc2};
                result.path = temp.second;
            }
        }
        if(br && pr1 + 1 < N) {
            locked[pr1 + cnt][pc1 + cnt] = 1;
            locked[pr2 + cnt][pc2 + cnt] = 1;
            temp = search_pair(initial_grid, pr1 + 1, pc1, pr1 + 1, pc2, 
                0, N - 1, 0, N - 1);
            locked[pr1 + cnt][pc1 + cnt] = 0;
            locked[pr2 + cnt][pc2 + cnt] = 0;
            if(temp.first < result.cost) {
                result.cost = temp.first;
                result.position = {pr1, pc1, pr1 + 1, pc2};
                result.path = temp.second;
            }
        }
    }
    
    if(result.cost == 1000) {
        result.position = {-1, -1, -1, -1};
        result.path = {};
        if(DEBUG) {
           //cout << "Extend failed, cost=1000" << endl;
        }
    } else {
        if(DEBUG) {
            //cout << "Extend succeeded, cost=" << result.cost << endl;
        }
    }
    
    return result;
}

pair<int, vector<Rotation>> move_extend_free_pair(const vector<vector<uint16_t>> &initial_grid,
                                                    int tlr, int tlc, int brr, int brc,
                                                    int target_r, int target_c) {
    if(DEBUG) {
        //cout << endl << "Moving Extended pair with cost: " << endl;
    }
    const int beam_width = 30;
    const int max_depth = 5;
    int N = initial_grid.size();
    vector<State> current_beam = {{initial_grid, {}, {tlr, tlc}}}; //used top left row and top left column to track (make sure to move entire box though)
    
    for(int depth = 0; depth < max_depth; depth++) {
        vector<State> next_beam;

        for(const auto &cur : current_beam) {
            if (target_r == cur.pos.first && target_c == cur.pos.second)
            {
                return {static_cast<int>(cur.path.size()), cur.path};
            }
        }

#pragma omp parallel
        {
            vector<State> local_beam;

#pragma omp for schedule(dynamic) nowait
            for(int beam_idx = 0; beam_idx < current_beam.size(); beam_idx++) {
                const auto &cur = current_beam[beam_idx];

                for (int k = 2; k <= N - 1; ++k)
                {
                    for (int r = 0; r <= N - k; ++r)
                    {
                        for (int c = 0; c <= N - k; ++c)
                        {
                            if (!Check_Valid(r + cnt, c + cnt, k - 1))
                                continue;
                            
                            bool affects = false;
                            auto [c_tlr, c_tlc] = cur.pos;
                            if (c_tlr >= r  && c_tlc >= c && c_tlr+1 < r + k && c_tlc+1 < c + k) affects = true; //check if the extended pair is in the rotation or not
                            if (!affects) continue;
                            vector<vector<uint16_t>> new_grid = rotate_submatrix(cur.grid, k, r, c);
                            
                            vector<Rotation> new_path = cur.path;
                            new_path.emplace_back(k, r, c);
                            local_beam.push_back({new_grid, new_path, {r - c + cur.pos.second, r + c + k - 2 - cur.pos.first}});
                        }
                    }
                }
            }
        
#pragma omp critical 
            {
                next_beam.insert(next_beam.end(), local_beam.begin(), local_beam.end());
            }
        }

        sort(next_beam.begin(), next_beam.end(),
             [&](const State &a, const State &b)
             {
                 int dist_a = abs(a.pos.first - target_r) + abs(a.pos.second - target_c) + 2 * a.path.size();
                 int dist_b = abs(b.pos.first - target_r) + abs(b.pos.second - target_c) + 2 * b.path.size();
                 return dist_a < dist_b;
             });
        if (next_beam.size() > static_cast<size_t>(beam_width))
        {
            next_beam.resize(beam_width);
        }
        current_beam = std::move(next_beam);
        if (current_beam.empty())
            break;
    }

    return {1000, {}};
}

pair<int, vector<Rotation>> Vertical_place(vector<vector<uint16_t>> grid, int Fsize, int j)
{
    int row = Fsize / 2 - 2;
    int min_ops = 999;
    vector<Rotation> partial_result;
    if (grid[row][j] == grid[row + 1][j])
        return {0, {}};

    if (SET_FP)
    {
        auto free_pairs = find_free_pairs(grid, j);
        cout << "V Found " << free_pairs.size() << " free pairs. ";

        for (const auto &[pr1, pc1, pr2, pc2, val] : free_pairs)
        {
            if (pr1 <= row)
                continue;
            int min_col = min(pc1, pc2);
            if (pr1 == pr2 && min_col >= j)
            {
                int k = pr1 - row + min_col - j + 1, r = row - (min_col - j);
                if (k>12 || r<0 ||j+k>=Fsize)
                    continue;
                if (locked[r + cnt][j + cnt + k - 1] || locked[r + cnt+k-1][j + cnt + k - 1])
                    continue;
                partial_result.emplace_back(k, r, j);
                cout << "Using free pair (" << pr1 << "," << pc1 << ")-(" << pr2 << "," << pc2 << ")";
                return {1, partial_result};
            }
            // auto move_result = move_free_pair_to_target(grid, pr1, pc1, pr2, pc2, i, j, true);
            // if (move_result.first < min_ops)
            // {
            //     partial_result = move_result.second;
            //     min_ops = move_result.first;
            //     cout << "Using free pair (" << pr1 << "," << pc1 << ")-(" << pr2 << "," << pc2 << ") with cost " << min_ops << ". ";
            // }
        }
    }

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
    if (min_ops <= 1)
        return {min_ops, partial_result};

    // int up = (!locked[cnt][cnt + Fsize / 2]) * 2, down = (!locked[cnt + Fsize - 1][cnt + Fsize / 2]) * (2 + (j >= row - 1) * (row - j - 2));
    // if (j == row + 1)
    //     down = -1;

    // /// down
    // for (int step = i + 1; step < Fsize - 2 + down; step++)
    // {
    //     temp_d = search_pair(grid, step, j, step, j + 1);
    //     if (temp_d.first + 1 < min_ops)
    //     {
    //         partial_result = temp_d.second;
    //         partial_result.emplace_back(step - i + 1, i, j);
    //         min_ops = temp_d.first + 1;
    //     }

    //     temp_d = search_pair(grid, step, j + 1, step, j);
    //     if (temp_d.first + 1 < min_ops)
    //     {
    //         partial_result = temp_d.second;
    //         partial_result.emplace_back(step - i + 1, i, j);
    //         min_ops = temp_d.first + 1;
    //     }
    //     if (min_ops <= 2)
    //         return {min_ops, partial_result};
    // }

    // // up
    // for (int step = j + 1; step < i + j - 1 + up; step++)
    // {
    //     temp_d = search_pair(grid, i + 1, step, i + 1, step + 1);
    //     if (temp_d.first + 1 < min_ops)
    //     {
    //         partial_result = temp_d.second;
    //         partial_result.emplace_back(step - j + 2, i - step + j, j);
    //         min_ops = temp_d.first + 1;
    //     }

    //     temp_d = search_pair(grid, i + 1, step + 1, i + 1, step);
    //     if (temp_d.first + 1 < min_ops)
    //     {
    //         partial_result = temp_d.second;
    //         partial_result.emplace_back(step - j + 2, i - step + j, j);
    //         min_ops = temp_d.first + 1;
    //     }
    //     if (min_ops <= 2)
    //         return {min_ops, partial_result};
    // }
    int err = 0;
    for (int i = Fsize-3; i > row ; i--)
    {
        for (int step = Fsize/2 -5+j;step>=j;step--)
        {
            auto [ops1, path1] = search_pair(grid, i, step+1, i, step, 0, Fsize - 1, 0, Fsize - 1);
            if(ops1 != 1){
                auto [ops1t, path1t] = search_pair(grid, i, step, i, step + 1, 0, Fsize - 1, 0, Fsize - 1);
                if (ops1t != 1)continue;
                
                ops1 = ops1t;
                path1 = path1t;
            }
            
            if(ops1 == 1)
            {
                int k = i - row + step - j + 1, r = row - (step - j);
                if (k > 12 || r < 0 || j + k >= Fsize){
                    err++;
                    cout<<'\n'<< k<<' '<< r <<' '<< j<<' ';
                    continue;}
                if (locked[r + cnt][j + cnt + k - 1] || locked[r + cnt + k - 1][j + cnt + k - 1])
                {
                    err++;
                    continue;}
                cout << " ERR: " << err << " ";
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
        partial_result = path1;
        partial_result.insert(partial_result.end(), path2.begin(), path2.end());

        locked[cnt + row][cnt + j] = 0;
        locked[cnt + row][cnt + j + 1] = 0;
        min_ops = ops1 + ops2;
    }

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
        }

        locked[cnt + row + 1][cnt + j] = 0;
        locked[cnt + row + 1][cnt + j + 1] = 0;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////

    if (min_ops <= 2)
    {
        return {min_ops, partial_result};
    }

    // Setting
    int up = (!locked[cnt][cnt + Fsize / 2]) * 2, down = (!locked[cnt + Fsize - 1][cnt + Fsize / 2]) * (2 + (j >= row - 1) * (row - j - 2));
    if (j == row + 1)
        down = -1;

    ////////////////////////////////////////////////////////////////////////////////////////////////////

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
            }

            locked[cnt + step][cnt + j + 1] = 0;
            locked[cnt + step + 1][cnt + j + 1] = 0;
        }
    }
    ////////////////////////////////////////////////////////////////////////////////////////////////////
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
            }

            locked[cnt + step + 1][cnt + j] = 0;
            locked[cnt + step + 1][cnt + j + 1] = 0;
        }
    }

    // // Up 1
    // for (int step = j + 1; step < row + j - 1 + up; step++)
    // {
    //     auto [ops1, path1] = search_pair(grid, row + 1, step, row + 1, step + 1);
    //     // if (temp_d.first + 1 < min_ops)
    //     // {
    //     //     partial_result = temp_d.second;
    //     //     partial_result.emplace_back(step - j + 2, i - step + j, j);
    //     //     min_ops = temp_d.first + 1;
    //     // }
    // }

    SET_FP = true;
    if (SET_FP) {

        for (const auto &[pr1, pc1, pr2, pc2, val] : free_pairs) {
            bool is_upper = max(pr1,pr2) < (Fsize/2 - 2);
            bool is_significant = !(locked[row + cnt][cnt + pc1] || locked[row + cnt][cnt + pc2] || locked[row + 1 + cnt][cnt + pc1] || locked[row + 1 + cnt][cnt + pc2]);
            if(DEBUG) {
                //cout << "Free pair (" << pr1 << "," << pc1 << ")-(" << pr2 << "," << pc2 << ") is_significant: " << is_significant << endl;
            }
            
            if(is_significant) {
                if(DEBUG) {
                    //cout << "Can be Extended" << endl;
                }
                bool is_vertical_pair = pc1 == pc2;
                bool tl = true, bl = true, tr = true, br = true;
                ExtendedPair extend_result;
                if(is_vertical_pair) {
                    tl = (pr1 >= 0 && pc1 - 1 >= 0 && 
                        !locked[pr1 + cnt][pc1 - 1 + cnt] && 
                        !locked[pr2 + cnt][pc1 - 1 + cnt]);
                
                    bl = (pr2 < Fsize  && pc1 - 1 >= 0 && 
                        !locked[pr1 + cnt][pc1 - 1 + cnt] && 
                        !locked[pr2 + cnt][pc1 - 1 + cnt]);
                    
                    tr = (pr1 >= 0 && pc1 + 1 < Fsize && 
                        !locked[pr1 + cnt][pc1 + 1 + cnt] && 
                        !locked[pr2 + cnt][pc1 + 1 + cnt]);
                    
                    br = (pr2 < Fsize  && pc1 + 1 < Fsize && 
                        !locked[pr1 + cnt][pc1 + 1 + cnt] && 
                        !locked[pr2 + cnt][pc1 + 1 + cnt]);
                    
                    if(tl || tr || bl || br) extend_result = extend_free_pair(grid, pr1, pr2, pc1, pc2, tl, bl, tr, br, true);
                } else {
                    tl = (pr1 - 1 >= 0 && pc1 >= 0 && 
                        !locked[pr1 - 1 + cnt][pc1 + cnt] && 
                        !locked[pr1 - 1 + cnt][pc2 + cnt]);
                    
                    tr = (pr1 - 1 >= 0 && pc2 < Fsize && 
                        !locked[pr1 - 1 + cnt][pc1 + cnt] && 
                        !locked[pr1 - 1 + cnt][pc2 + cnt]);
                    
                    bl = (pr1 + 1 < Fsize && pc1 >= 0 && 
                        !locked[pr1 + 1 + cnt][pc1 + cnt] && 
                        !locked[pr1 + 1 + cnt][pc2 + cnt]);
                    
                    br = (pr1 + 1 < Fsize && pc2 < Fsize && 
                        !locked[pr1 + 1 + cnt][pc1 + cnt] && 
                        !locked[pr1 + 1 + cnt][pc2 + cnt]);
                    
                    if(tl || tr || bl || br) extend_result = extend_free_pair(grid, pr1, pr2, pc1, pc2, tl, bl, tr, br, false);
                }
                
                auto [tlr, tlc, brr, brc] = extend_result.position;
                if(DEBUG) {
                    //cout << tlr << " " << tlc << " " << brr << " " << brc << endl;
                    cout << "EXT RES: " << extend_result.cost << " ";
                }
                if(tlr != -1 && tlc != -1 && brr != -1 && brc != -1) {
                    pair<int, vector<Rotation>> move_extend_free_pair_result = move_extend_free_pair(apply_rotations(grid, extend_result.path), tlr, tlc, brr, brc, row, j);
                    extend_result.path.insert(extend_result.path.end(), move_extend_free_pair_result.second.begin(), move_extend_free_pair_result.second.end());
                    extend_result.cost += move_extend_free_pair_result.first;
                    if (DEBUG)
                    {
                        //cout << move_extend_free_pair_result.first << endl;
                    }

                    if (extend_result.cost < min_ops)
                    {
                        partial_result = extend_result.path;
                        min_ops =extend_result.cost;
                        cout << "Using extend free pair (" << tlr << "," << tlc << ")-(" << brr << "," << brc << ") with cost " << min_ops << ". ";
                        // if (move_extend_free_pair_result.first <= 2)
                        // {
                        //     return {min_ops, partial_result};
                        // }
                    }
                }
            } 

            //auto move_result = move_free_pair_to_target(grid, pr1, pc1, pr2, pc2, row, j, true);
            //if (move_result.first < min_ops)
            //{
            //    partial_result = move_result.second;
            //    min_ops = move_result.first;
            //    cout << "Using free pair (" << pr1 << "," << pc1 << ")-(" << pr2 << "," << pc2 << ") with cost " << min_ops << ". ";
            //}
        }
    }

    return {min_ops, partial_result};
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

pair<vector<vector<uint16_t>>, vector<Rotation>> STEP_Do(int Fsize, vector<vector<uint16_t>> grid, int mode)
{
    int half = Fsize / 2;
    int row = half - 2;

    vector<int> dp(half + 2, 0);
    vector<pair<vector<vector<uint16_t>>, vector<Rotation>>> result(half + 1, {grid, {}});
    pair<int, vector<Rotation>> V_result = Vertical_place(result[mode].first, Fsize, mode);
    int V_ops = dp[mode] + V_result.first;
    locked[cnt + row][cnt + mode] = 1;
    locked[cnt + row + 1][cnt + mode] = 1;

    dp[mode + 1] = V_ops;
    result[mode + 1].first = apply_rotations(result[mode].first, V_result.second);

    for (auto &rot : V_result.second)
    {
        result[mode + 1].second.emplace_back(rot.k, rot.i + cnt, rot.j + cnt);
    }
    cout << "(f)cost : " << V_ops << '\n';

    for (int j = mode + 2; j <= half; j += 1)
    {
        locked[cnt + row][cnt + j - 2] = 0;
        locked[cnt + row + 1][cnt + j - 2] = 0;
        locked[cnt + row][cnt + j - 1] = 0;
        locked[cnt + row + 1][cnt + j - 1] = 0;

        pair<int, vector<Rotation>> H_result = Horizontal_place(result[j - 2].first, Fsize, j - 2);
        int H_ops = dp[j - 2] + H_result.first;
        locked[cnt + row][cnt + j - 2] = 1;
        locked[cnt + row + 1][cnt + j - 2] = 1;

        pair<int, vector<Rotation>> V_result = Vertical_place(result[j - 1].first, Fsize, j - 1);
        int V_ops = dp[j - 1] + V_result.first;
        locked[cnt + row][cnt + j - 1] = 1;
        locked[cnt + row + 1][cnt + j - 1] = 1;

        if (V_ops > H_ops)
        {
            dp[j] = H_ops;
            result[j].first = apply_rotations(result[j - 2].first, H_result.second);
            result[j].second = result[j - 2].second;
            for (auto &rot : H_result.second)
            {
                result[j].second.emplace_back(rot.k, rot.i + cnt, rot.j + cnt);
            }
            cout << "H (f)cost : " << H_result.first << '\n';
        }
        else
        {
            dp[j] = V_ops;
            result[j].first = apply_rotations(result[j - 1].first, V_result.second);
            result[j].second = result[j - 1].second;
            for (auto &rot : V_result.second)
            {
                result[j].second.emplace_back(rot.k, rot.i + cnt, rot.j + cnt);
            }
            cout << "V (f)cost : " << V_result.first << '\n';
        }
    }

    locked = rotate_submatrix_u8(locked, half, cnt, cnt);
    grid = rotate_submatrix(result[half].first, half, 0, 0);
    result[half].second.emplace_back(half, cnt, cnt);
    cout<<"Rotate half\n";

    return {grid, result[half].second};
}

int main()
{
    omp_set_num_threads(omp_get_max_threads());
    cout << "Using " << omp_get_max_threads() << " threads\n";

    vector<vector<uint16_t>> init_grid = get_random_board(24);
    // vector<vector<uint16_t>> init_grid ={
    //     {180, 129, 227, 266, 45, 253, 125, 115, 267, 66, 233, 152, 166, 31, 11, 248, 154, 146, 243, 219, 113, 17, 66, 83}, {36, 89, 200, 157, 87, 90, 37, 99, 33, 196, 38, 235, 258, 190, 114, 127, 140, 144, 254, 206, 50, 170, 172, 144}, {214, 13, 196, 118, 133, 135, 138, 126, 262, 21, 186, 267, 114, 160, 229, 204, 108, 138, 259, 195, 33, 184, 268, 47}, {40, 10, 8, 228, 236, 212, 75, 178, 197, 174, 29, 136, 215, 131, 208, 117, 56, 177, 235, 124, 179, 193, 141, 79}, {240, 68, 177, 225, 93, 282, 22, 39, 234, 240, 79, 259, 217, 192, 45, 156, 247, 222, 213, 122, 153, 127, 73, 80}, {85, 24, 53, 136, 67, 287, 168, 124, 140, 225, 270, 228, 5, 216, 1, 70, 249, 95, 216, 109, 169, 62, 98, 57}, {111, 220, 283, 141, 83, 30, 227, 76, 0, 261, 28, 218, 132, 84, 58, 23, 182, 103, 221, 105, 112, 253, 232, 71}, {7, 137, 97, 284, 232, 198, 89, 64, 274, 199, 151, 224, 147, 174, 59, 238, 223, 88, 192, 36, 159, 257, 95, 82}, {175, 165, 9, 175, 60, 256, 202, 222, 19, 186, 3, 51, 158, 272, 105, 183, 202, 160, 65, 34, 74, 49, 94, 173}, {231, 187, 210, 263, 120, 264, 92, 88, 203, 49, 110, 106, 97, 239, 113, 150, 52, 237, 59, 122, 153, 263, 200, 273}, {219, 128, 20, 280, 279, 135, 61, 184, 168, 11, 131, 35, 30, 102, 158, 252, 230, 130, 213, 237, 143, 29, 13, 211}, {146, 283, 101, 185, 260, 46, 209, 285, 44, 268, 9, 238, 72, 43, 161, 191, 128, 189, 226, 77, 272, 91, 285, 145}, {107, 120, 3, 205, 176, 254, 102, 265, 276, 63, 190, 72, 162, 147, 42, 109, 245, 148, 210, 98, 173, 16, 246, 265}, {156, 94, 275, 250, 100, 281, 111, 126, 32, 61, 28, 17, 73, 139, 271, 1, 104, 119, 209, 80, 145, 208, 161, 134}, {7, 76, 4, 71, 250, 275, 154, 211, 244, 22, 75, 180, 162, 82, 20, 60, 142, 55, 64, 284, 248, 42, 276, 181}, {14, 273, 116, 112, 262, 74, 224, 242, 149, 150, 69, 181, 123, 43, 103, 226, 78, 54, 41, 269, 86, 53, 15, 207}, {266, 172, 198, 143, 215, 179, 23, 12, 260, 104, 41, 18, 229, 130, 35, 230, 106, 119, 271, 91, 205, 280, 155, 90}, {169, 157, 68, 26, 121, 12, 167, 249, 123, 233, 203, 19, 137, 257, 65, 116, 188, 183, 27, 270, 239, 251, 81, 55}, {24, 118, 256, 155, 134, 44, 117, 201, 107, 255, 286, 15, 142, 81, 236, 151, 14, 40, 170, 6, 194, 214, 62, 246}, {4, 77, 108, 48, 197, 125, 10, 281, 278, 46, 2, 178, 8, 159, 139, 93, 278, 148, 164, 258, 223, 218, 188, 251}, {252, 133, 6, 286, 129, 18, 37, 189, 185, 99, 176, 84, 51, 58, 100, 163, 221, 121, 5, 245, 164, 85, 149, 69}, {26, 48, 199, 67, 212, 277, 191, 92, 165, 217, 204, 21, 247, 16, 282, 152, 193, 166, 207, 241, 2, 25, 287, 220}, {70, 0, 274, 56, 32, 231, 96, 269, 206, 96, 277, 132, 63, 163, 34, 241, 101, 86, 243, 195, 264, 57, 52, 87}, {167, 31, 242, 244, 54, 187, 110, 38, 39, 194, 255, 171, 279, 261, 50, 201, 171, 234, 27, 182, 78, 25, 115, 47}
    // };
    vector<vector<uint16_t>> grid = init_grid;
    n = grid.size();
    locked = vector<vector<uint8_t>>(n, vector<uint8_t>(n, 0));
    vector<Rotation> full_path;

    for (int Fsize = n - cnt * 2; Fsize > n / 2; Fsize -= 4)
    {
        vector<vector<uint16_t>> crop;
        vector<Rotation> partial_path;
        crop.reserve(n - cnt * 2);

        for (int i = cnt; i < n - cnt; i++)
        {
            vector<uint16_t> row(grid[i].begin() + cnt, grid[i].end() - cnt);
            crop.push_back(row);
        }

        pair<vector<vector<uint16_t>>, vector<Rotation>> res = STEP_Do(Fsize, crop, 0);
        partial_path.insert(partial_path.end(), res.second.begin(), res.second.end());

        crop = rotate_submatrix(res.first, Fsize, 0, 0);
        locked = rotate_submatrix_u8(locked, Fsize, cnt, cnt);
        partial_path.emplace_back(Fsize, cnt, cnt);

        for (int i = 0; i < 3; i++)
        {
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
        cout << "crop (cnt = " << cnt << " )" << ":\n";
        print_grid(res.first);
        cout << "grid: \n";
        print_grid(grid);
        cout << "------------------------------------\n";

        cnt += 2;
    }

    cout << "-----------------           Before          -------------------\n";
    print_grid(init_grid);
    save_file(init_grid, full_path);
    cout << "-----------------           After           -------------------\n";
    init_grid = apply_rotations(init_grid, full_path);
    print_grid(init_grid);
    cout << "\n ops: " << full_path.size();

    if (broke)
        cout << "-------------------------------------BROKE----------------------------------\n";
    return 0;
}