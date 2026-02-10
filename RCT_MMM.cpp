#include <bits/stdc++.h>
#include <fstream>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <omp.h>

using namespace std;
using json = nlohmann::json;

vector<vector<uint8_t>> locked;
int n;
int cnt = 0;
int side = 0;
bool broke = false;
bool SET_FP = true;
bool DEBUG = false;

struct Rotation
{
    int k, i, j;
    Rotation(int k, int i, int j) : k(k), i(i), j(j) {}
};

struct State
{
    vector<vector<uint16_t>> grid;
    vector<Rotation> path;
    pair<int, int> pos;
};

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
    {
        grid = rotate_submatrix(grid, rot.k, rot.i, rot.j);
    }
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
        for (uint16_t val : row)
        {
            cout << setw(3) << val << " ";
        }
        cout << endl;
    }
}

bool Check_Valid(int i, int j, int k)
{
    if (k < 2 || i < 0 || j < 0)
        return false;
    if (locked[i][j] || locked[i + k][j + k] || locked[i][j + k] || locked[i + k][j])
        return false;
    for (int ft = 1; ft < k; ft += 1)
    {
        if (locked[i + ft][j] || locked[i][j + ft] || locked[i + ft][j + k] || locked[i + k][j + ft])
            return false;
    }
    return true;
}

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

vector<tuple<int, int, int, int, uint16_t>> find_free_pairs(const vector<vector<uint16_t>> &grid)
{
    vector<tuple<int, int, int, int, uint16_t>> pairs;
    int N = grid.size();

    for (int i = 0; i < N; ++i)
    {
        for (int j = 0; j < N; ++j)
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

pair<int, vector<Rotation>> search_pair(const vector<vector<uint16_t>> &initial_grid, int row1, int col1, int row2, int col2)
{
    const int beam_width = 30 + cnt * 5;
    const int max_depth = 5;
    int N = initial_grid.size();

    vector<State> current_beam = {{initial_grid, {}, find_pos(initial_grid, row1, col1)}};
    if (current_beam[0].pos.first == -1)
    {
        // cout << row1 << ' ' << col1 << " Broke\n";
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

pair<int, vector<Rotation>> SQ_spe(vector<vector<uint16_t>> grid, int Fsize, int i, int j, int mode) // if mode = 0 then top left , mode = 1 then down right
{
    int row = Fsize / 2 - 2;
    int min_ops = 999;
    vector<Rotation> partial_result;

    if (mode == 0)
    {
        auto [ops1, path1] = search_pair(grid, i, j, i, j + 1);

        locked[cnt + i][cnt + j] = 1;
        locked[cnt + i][cnt + j + 1] = 1;

        auto [ops2, path2] = search_pair(apply_rotations(grid, path1), i + 1, j, i + 1, j + 1);
        partial_result = path1;
        partial_result.insert(partial_result.end(), path2.begin(), path2.end());

        locked[cnt + i][cnt + j] = 0;
        locked[cnt + i][cnt + j + 1] = 0;
        min_ops = ops1 + ops2;
    }

    if (mode == 1)
    {

        auto [ops1, path1] = search_pair(grid, i + 1, j + 1, i + 1, j);

        locked[cnt + i + 1][cnt + j] = 1;
        locked[cnt + i + 1][cnt + j + 1] = 1;

        auto [ops2, path2] = search_pair(apply_rotations(grid, path1), i, j + 1, i, j);
        if (ops1 + ops2 < min_ops)
        {
            min_ops = ops1 + ops2;
            partial_result = path1;
            partial_result.insert(partial_result.end(), path2.begin(), path2.end());
        }

        locked[cnt + i + 1][cnt + j] = 0;
        locked[cnt + i + 1][cnt + j + 1] = 0;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////
    // First 3
    {
        auto [ops1, path1] = search_pair(grid, i + mode, j + mode, i + !mode, j + mode);

        locked[cnt + i][cnt + j] = 1;
        locked[cnt + i + 1][cnt + j] = 1;

        auto grid_temp = apply_rotations(grid, path1);
        auto [ops2, path2] = search_pair(grid_temp, i + mode, j + !mode, i + !mode, j + !mode);

        if (ops1 + ops2 < min_ops)
        {
            min_ops = ops1 + ops2;
            partial_result = path1;
        }

        locked[cnt + i][cnt + j] = 0;
        locked[cnt + i + 1][cnt + j] = 0;
    }
    return {min_ops, partial_result};
}
pair<vector<vector<uint16_t>>, vector<Rotation>> DODAI(int Fsize, vector<vector<uint16_t>> grid)
{
    int td = 2, tr = 2;
    int du = 2, dl = 2;
    vector<Rotation> partial_result;

    pair<int, vector<Rotation>> temp_d = SQ_spe(grid, Fsize, 0, 0, 0);
    partial_result = temp_d.second;
    locked[cnt][cnt ] = 1;
    locked[cnt][cnt+1] = 1;
    locked[cnt+1][cnt] = 1;
    locked[cnt + 1][cnt+1] = 1;
    cout<<"C1! : "<<temp_d.first<<'\n';
    pair<int, vector<Rotation>> temp_d = SQ_spe(grid, Fsize, 0, 0, 0);
    partial_result.insert(partial_result.end(), temp_d.second.begin(), temp_d.second.end());
    locked[cnt + Fsize - 2][cnt + Fsize - 2] = 1;
    locked[cnt + Fsize - 2][cnt + Fsize - 1] = 1;
    locked[cnt + Fsize - 1][cnt + Fsize - 2] = 1;
    locked[cnt + Fsize - 1][cnt + Fsize - 1] = 1;
    cout << "C2! : " << temp_d.first << '\n';

    //////////////////////////////////////////////////
    /////////////////  NOT FINISH  ///////////////////
    //////////////////////////////////////////////////

    return {{}, {}};
}

pair<vector<vector<uint16_t>>, vector<Rotation>> DO_DOWN(int Fsize, vector<vector<uint16_t>> grid)
{


    //////////////////////////////////////////////////
    /////////////////  NOT FINISH  ///////////////////
    //////////////////////////////////////////////////

    return {{}, {}};
}

pair<vector<vector<uint16_t>>, vector<Rotation>> DO_UP(int Fsize, vector<vector<uint16_t>> grid)
{

    //////////////////////////////////////////////////
    /////////////////  NOT FINISH  ///////////////////
    //////////////////////////////////////////////////

    return {{}, {}};
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

        pair<vector<vector<uint16_t>>, vector<Rotation>> res = DODAI(Fsize, crop);
        partial_path.insert(partial_path.end(), res.second.begin(), res.second.end());

        pair<vector<vector<uint16_t>>, vector<Rotation>> res = DO_DOWN(Fsize, res.first);
        partial_path.insert(partial_path.end(), res.second.begin(), res.second.end());

        pair<vector<vector<uint16_t>>, vector<Rotation>> res = DO_UP(Fsize, res.first);
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