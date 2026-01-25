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

vector<vector<uint8_t>> locked;
int n;
int cnt = 0;
int side = 0;
bool quit = false;
bool broke = false;
bool SET_FP = true;

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
    for (int ft = 1; ft < k; ft +=1)
    {
        if (locked[i + ft][j] || locked[i][j + ft] || locked[i + ft][j + k] || locked[i + k][j + ft])
            return false;
    }
    return true;
}

bool show_prompt(vector<vector<uint16_t>> grid)
{
    int choice = 0;
    bool check = false;
    while (choice != 4)
    {
        cout << "\n1: Show grid | 2: Show locked | 3: count adjacent pair | 4: continune | 5: quit\n> ";
        cin >> choice;
        switch (choice)
        {
        case 1:
            print_grid(grid);
            break;
        case 2:
            print_grid(locked);
            break;
        case 3:
            cout << "Adjacent pairs: " << count_adjacent_pairs(grid) << endl;
            break;
        case 4:
            break;
        case 5:
            check = true;
            break;
        default:
            cout << "Invalid choice\n";
        }
        if (check)
            break;
    }
    return check;
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
    const int beam_width = 40;
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

pair<int, vector<Rotation>> search_pair(const vector<vector<uint16_t>> &initial_grid, int row1, int col1, int row2, int col2)
{
    const int beam_width = 80 + cnt * 5;
    const int max_depth = 5;
    int N = initial_grid.size();

    vector<State> current_beam = {{initial_grid, {}, find_pos(initial_grid, row1, col1)}};
    if (current_beam[0].pos.first == -1)
    {
        cout << row1 << ' ' << col1 << " Broke\n";
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
                    for (int r = 0; r <= N - k; ++r)
                    {
                        for (int c = 0; c <= N - k; ++c)
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

pair<int, vector<Rotation>> Vertical_place(vector<vector<uint16_t>> grid, int Fsize, int j,int i)
{
    int row = Fsize / 2 - 2;
    int min_ops = 999;
    vector<Rotation> partial_result;

    pair<int, vector<Rotation>> temp_d = search_pair(grid, i, j, i + 1, j);
    if (temp_d.first < min_ops)
    {
        partial_result = temp_d.second;
        min_ops = temp_d.first;
    }
    temp_d = search_pair(grid, i + 1, j, i, j);
    if (temp_d.first < min_ops)
    {
        partial_result = temp_d.second;
        min_ops = temp_d.first;
    }
    if (min_ops <= 1)
        return {min_ops, partial_result};

    if(SET_FP)
    {
        auto free_pairs = find_free_pairs(grid, j);
        cout << "V Found " << free_pairs.size() << " free pairs. ";
    
        for (const auto &[pr1, pc1, pr2, pc2, val] : free_pairs)
        {
            auto move_result = move_free_pair_to_target(grid, pr1, pc1, pr2, pc2, i, j, true);
            if (move_result.first < min_ops)
            {
                partial_result = move_result.second;
                min_ops = move_result.first;
                cout << "Using free pair (" << pr1 << "," << pc1 << ")-(" << pr2 << "," << pc2 << ") with cost " << min_ops << ". ";
            }
        }
    }

    if (min_ops <= 1)
        return {min_ops, partial_result};

    int up=0, down = (!locked[cnt + Fsize - 1][cnt + Fsize/2])*(2 + (j>=row-1)*(row-j-2) );

    ///down
    for (int step = i + 1; step < Fsize - 2 + down; step++)
    {
        temp_d = search_pair(grid, step, j, step, j + 1);
        if (temp_d.first + 1 < min_ops)
        {
            partial_result = temp_d.second;
            partial_result.emplace_back(step - i + 1, i, j);
            min_ops = temp_d.first + 1;
        }

        temp_d = search_pair(grid, step, j + 1, step, j);
        if (temp_d.first + 1 < min_ops)
        {
            partial_result = temp_d.second;
            partial_result.emplace_back(step - i + 1, i, j);
            min_ops = temp_d.first + 1;
        }
    }
    if (min_ops <= 1)
        return {min_ops, partial_result};

    //up
    for (int step = j + 1; step < i + j - 1+ up; step++)
    {
        temp_d = search_pair(grid, i + 1, step, i + 1, step + 1);
        if (temp_d.first + 1 < min_ops)
        {
            partial_result = temp_d.second;
            partial_result.emplace_back(step - j + 2, i - step + j, j);
            min_ops = temp_d.first + 1;
        }

        temp_d = search_pair(grid, i + 1, step + 1, i + 1, step);
        if (temp_d.first + 1 < min_ops)
        {
            partial_result = temp_d.second;
            partial_result.emplace_back(step - j + 2, i - step + j, j);
            min_ops = temp_d.first + 1;
        }
    }

    return {min_ops, partial_result};
}

pair<int, vector<Rotation>> Horizontal_place(vector<vector<uint16_t>> grid, int Fsize, int j)
{
    int row = Fsize / 2 - 2;
    int min_ops = 999;
    vector<Rotation> partial_result,temp_result;
    auto free_pairs = find_free_pairs(grid, j);
    cout << "H Found " << free_pairs.size() << " free pairs. ";
    SET_FP = true;

    //First 1
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

        auto [ops1, path1] = search_pair(grid, row, j, row, j + 1);
        
        locked[cnt + row][cnt + j] = 1;
        locked[cnt + row][cnt + j + 1] = 1;
        
        if(ops1 > mn_free)
        {
            ops1 = mn_free;
            path1 = temp_result;
        }

        auto [ops2, path2] = search_pair(apply_rotations(grid, path1), row + 1, j, row + 1, j + 1);
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
            auto move_result = move_free_pair_to_target(grid, pr1, pc1, pr2, pc2, row+1, j, false);
            if (move_result.first < mn_free)
            {
                temp_result = move_result.second;
                mn_free = move_result.first;
            }
        }

        auto [ops1, path1] = search_pair(grid, row + 1, j, row + 1, j + 1);

        locked[cnt + row+1][cnt + j] = 1;
        locked[cnt + row+1][cnt + j + 1] = 1;

        if (ops1 > mn_free)
        {
            ops1 = mn_free;
            path1 = temp_result;
        }

        auto [ops2, path2] = search_pair(apply_rotations(grid, path1), row, j, row, j + 1);
        if (ops1 + ops2 < min_ops){
            min_ops = ops1 + ops2;
            partial_result = path1;
            partial_result.insert(partial_result.end(), path2.begin(), path2.end());
        }

        locked[cnt + row+1][cnt + j] = 0;
        locked[cnt + row+1][cnt + j + 1] = 0;
    }

    if (min_ops <= 2)
    {
        return {min_ops, partial_result};
    }



    // Setting
    int up = 0, down = (!locked[cnt + Fsize - 1][cnt + Fsize / 2]) * (2 + (j >= row - 1) * (row - j - 2));
    // Down 1

    {
        // for (const auto &[pr1, pc1, pr2, pc2, val] : free_pairs)
        // {
        //     auto move_result = move_free_pair_to_target(grid, pr1, pc1, pr2, pc2, row + 1, j, false);
        //     if (move_result.first < mn_free)
        //     {
        //         temp_result = move_result.second;
        //         mn_free = move_result.first;
        //         cout << "Using free pair (" << pr1 << "," << pc1 << ")-(" << pr2 << "," << pc2 << ") with cost " << mn_free << ". ";
        //     }
        // }
        //int mn_free = 999;

        SET_FP = false;
        for (int step = row + 1; step < Fsize - 3 + down; step++)
        {

            auto [ops1, path1] = search_pair(grid, step , j, step + 1, j );
            locked[cnt + step][cnt + j] = 1;
            locked[cnt + step+1][cnt + j] = 1;

            // if (ops1 > mn_free)
            // {
            //     ops1 = mn_free;
            //     path1 = temp_result;
            // }

            auto [ops2, path2] = search_pair(apply_rotations(grid, path1), step, j+1, step + 1, j+1);

            if (ops1 + ops2 + 1 < min_ops)
            {
                min_ops = ops1 + ops2+1;
                partial_result = path1;
                partial_result.insert(partial_result.end(), path2.begin(), path2.end());
                partial_result.emplace_back(step - row + 2, row, j);
            }

            locked[cnt + step][cnt + j] = 0;
            locked[cnt + step + 1][cnt + j] = 0;
        }
    }
    

    

    //Down

    SET_FP = true;

    return {min_ops , partial_result};
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
    pair<int, vector<Rotation>> V_result = Vertical_place(result[mode].first, Fsize, mode,row);
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

        pair<int, vector<Rotation>> V_result = Vertical_place(result[j - 1].first, Fsize, j - 1, row);
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

    return {grid, result[half].second};
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