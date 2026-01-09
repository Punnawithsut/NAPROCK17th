#include <bits/stdc++.h>
#include <iomanip>
using namespace std;

struct Rotation
{
    int k, i, j;
    Rotation(int k, int i, int j) : k(k), i(i), j(j) {}
};

vector<vector<int>> locked;

int n;
int cnt = 0;
int side = 0;
bool quit = false;
bool broke = false;

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

void print_grid(const vector<vector<int>> &grid)
{
    for (const auto &row : grid)
    {
        for (int val : row)
        {
            cout << setw(3) << val << " ";
        }
        cout << endl;
    }
}
vector<vector<int>> apply_rotations(vector<vector<int>> grid, const vector<Rotation> &path)
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
    for (int ft = 2; ft < k; ft += 2)
    {
        if (locked[i + ft][j] || locked[i][j + ft] || locked[i + ft][j + k] || locked[i + k][j + ft])
            return false;
    }
    return true;
}

bool show_prompt(vector<vector<int>> grid)
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

string serialize(const vector<vector<int>> &g)
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
    vector<vector<int>> grid;
    vector<Rotation> path;
    pair<int, int> pos;
};

pair<int, int> find_pos(const vector<vector<int>> &grid, int row, int col)
{
    if (grid.empty() || row < 0 || col < 0 || row >= grid.size() || col >= grid[0].size())
        return make_pair(-1, -1);

    int target = grid[row][col];
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

pair<int, vector<Rotation>> search_pair(const vector<vector<int>> &initial_grid, int row1, int col1, int row2, int col2)
{
    const int beam_width = 10;
    const int max_depth = 10;
    int N = initial_grid.size();
    unordered_set<string> visited;

    vector<State> current_beam = {{initial_grid, {}, find_pos(initial_grid, row1, col1)}};
    if (current_beam[0].pos.first == -1)
    {
        cout << row1 << ' ' << col1 << " Broke\n";
        broke = true;
        return {1000, {}};
    }
    visited.insert(serialize(initial_grid));

    for (int depth = 0; depth < max_depth; ++depth)
    {
        vector<State> next_beam;
        for (const auto &cur : current_beam)
        {
            if (cur.grid[row1][col1] == cur.grid[row2][col2])
            {
                return {static_cast<int>(cur.path.size()), cur.path};
            }
            for (int k = min(max(abs(cur.pos.first - row2), abs(cur.pos.second - col2)) + 2, 24); k >= 2; --k)
            {
                for (int r = 0; r <= N - k; ++r)
                {
                    for (int c = 0; c <= N - k; ++c)
                    {
                        if ((r <= row1 && row1 < r + k && c <= col1 && col1 < c + k) || !Check_Valid(r + cnt, c + cnt, k - 1))
                            continue;
                        // Prune: only rotations affecting target cell
                        bool affects = (r <= row2 && row2 < r + k && c <= col2 && col2 < c + k);

                        if (!affects)
                            continue;
                        vector<vector<int>> new_grid = rotate_submatrix(cur.grid, k, r, c);
                        string ser = serialize(new_grid);

                        if (visited.find(ser) == visited.end())
                        {
                            visited.insert(ser);
                            vector<Rotation> new_path = cur.path;
                            new_path.emplace_back(k, r, c);
                            next_beam.push_back({new_grid, new_path, {r - c + cur.pos.second, r + c + k - 1 - cur.pos.first}});
                        }
                    }
                }
            }
        }

        sort(next_beam.begin(), next_beam.end(),
             [&](const State &a, const State &b)
             {
                 int dist_a = abs(a.pos.first - row2) + abs(a.pos.second - col2) + 2 * a.path.size();
                 int dist_b = abs(b.pos.first - row2) + abs(b.pos.second - col2) + 2 * b.path.size();
                 return dist_a < dist_b; // smaller first
             });
        // Prune to beam_width
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

pair<int, vector<Rotation>> Vertical_place(vector<vector<int>> grid, int Fsize, int j)
{
    int row = Fsize / 2 - 2;
    int min_ops = 999;
    vector<Rotation> partial_result;
    pair<int, vector<Rotation>> temp_d = search_pair(grid, row, j, row + 1, j);
    if (temp_d.first < min_ops)
    {
        partial_result = temp_d.second;
        min_ops = temp_d.first;
    }
    temp_d = search_pair(grid, row + 1, j, row, j);
    if (temp_d.first < min_ops)
    {
        partial_result = temp_d.second;
        min_ops = temp_d.first;
    }

    for (int step = row + 1; step < Fsize - 2; step++) // it can be optimize further but I am lazy
    {
        temp_d = search_pair(grid, step, j, step, j + 1);
        if (temp_d.first + 1 < min_ops)
        {
            partial_result = temp_d.second;
            partial_result.emplace_back(step - row + 1, row, j);
            min_ops = temp_d.first + 1;
        }

        temp_d = search_pair(grid, step, j + 1, step, j);
        if (temp_d.first + 1 < min_ops)
        {
            partial_result = temp_d.second;
            partial_result.emplace_back(step - row + 1, row, j);
            min_ops = temp_d.first + 1;
        }
    }

    return {min_ops, partial_result};
}
pair<int, vector<Rotation>> Horizontal_place(vector<vector<int>> grid, int Fsize, int j)
{
    int row = Fsize / 2 - 2;
    vector<Rotation> partial_result;
    
    // First pair placement
    auto [ops1, path1] = search_pair(grid, row, j, row, j + 1);
    partial_result = path1;
    
    grid = apply_rotations(grid, partial_result);
    locked[cnt + row][cnt + j] = 1;
    locked[cnt + row][cnt + j + 1] = 1;
    
    // Second pair placement
    auto [ops2, path2] = search_pair(grid, row + 1, j, row + 1, j + 1);
    partial_result.insert(partial_result.end(), path2.begin(), path2.end());
    
    locked[cnt + row][cnt + j] = 0;
    locked[cnt + row][cnt + j + 1] = 0;
    
    return {ops1 + ops2, partial_result};
}

pair<vector<vector<int>>, vector<Rotation>> STEP_Do(int Fsize, vector<vector<int>> grid, int mode)
{
    int half = Fsize / 2;
    int row = half - 2;

    int dp[half + 2] = {0};
    vector<pair<vector<vector<int>>, vector<Rotation>>> result(half + 1, {grid, {}});
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

        //pair<int, vector<Rotation>> H_result = Horizontal_place(result[j - 2].first, Fsize, j - 2);
         pair<int, vector<Rotation>> H_result = {999,{}};
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
            cout << "2 (f)cost : " << H_result.first << '\n';
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
            cout << "1 (f)cost : " << V_result.first << '\n';
        }
        
    }

    locked = rotate_submatrix(locked, half, cnt, cnt);
    grid = rotate_submatrix(result[half].first, half, 0, 0);
    result[half].second.emplace_back(half, cnt, cnt);

    return {grid, result[half].second};
}

int main()
{

    vector<vector<int>> init_grid = {
        {33, 78, 266, 95, 82, 52, 20, 242, 203, 19, 81, 200, 5, 120, 47, 102, 220, 184, 190, 272, 283, 134, 114, 183},
        {218, 265, 73, 83, 133, 205, 110, 146, 223, 184, 29, 48, 103, 160, 231, 39, 122, 60, 264, 57, 24, 24, 107, 12},
        {237, 239, 68, 198, 144, 13, 151, 11, 202, 105, 44, 240, 8, 103, 6, 2, 51, 223, 17, 74, 34, 284, 250, 79},
        {15, 226, 164, 221, 104, 52, 56, 229, 7, 181, 252, 68, 100, 173, 200, 169, 110, 84, 208, 230, 56, 85, 115, 262},
        {185, 266, 161, 25, 83, 181, 180, 150, 194, 250, 238, 235, 180, 79, 237, 54, 161, 188, 72, 122, 273, 55, 206, 34},
        {18, 107, 143, 199, 168, 281, 25, 164, 220, 70, 281, 241, 243, 187, 170, 22, 244, 228, 167, 210, 39, 155, 282, 86},
        {102, 219, 189, 35, 47, 140, 267, 93, 156, 51, 214, 71, 59, 23, 244, 45, 116, 282, 69, 60, 129, 73, 283, 151},
        {12, 144, 187, 133, 154, 275, 49, 170, 210, 18, 280, 226, 130, 249, 69, 190, 135, 58, 135, 189, 118, 173, 208, 17},
        {61, 224, 165, 246, 177, 43, 146, 45, 29, 217, 11, 145, 100, 253, 90, 225, 241, 134, 256, 167, 76, 70, 275, 95},
        {271, 0, 284, 49, 178, 195, 197, 186, 88, 62, 57, 13, 22, 258, 247, 166, 132, 186, 115, 140, 105, 248, 44, 276},
        {258, 205, 129, 2, 286, 214, 77, 183, 128, 106, 116, 23, 26, 217, 206, 271, 267, 136, 84, 62, 194, 154, 117, 89},
        {171, 36, 71, 227, 120, 197, 74, 243, 232, 204, 75, 280, 1, 270, 196, 172, 10, 137, 230, 145, 27, 203, 89, 236},
        {155, 199, 169, 130, 38, 35, 204, 260, 16, 272, 119, 94, 182, 16, 1, 94, 101, 166, 174, 138, 97, 37, 48, 141},
        {108, 143, 211, 269, 174, 229, 38, 118, 42, 61, 278, 273, 252, 278, 235, 127, 279, 149, 249, 213, 276, 179, 212, 98},
        {168, 123, 66, 177, 163, 121, 231, 213, 261, 242, 37, 260, 179, 92, 158, 136, 248, 175, 201, 261, 31, 3, 4, 233},
        {153, 31, 198, 159, 3, 247, 64, 90, 157, 240, 59, 279, 202, 125, 93, 227, 287, 126, 239, 0, 218, 150, 64, 157},
        {185, 193, 254, 159, 98, 113, 131, 33, 121, 72, 264, 234, 257, 147, 225, 91, 127, 268, 46, 163, 81, 165, 86, 8},
        {14, 257, 124, 123, 191, 233, 78, 109, 221, 112, 246, 128, 274, 101, 109, 209, 222, 171, 285, 207, 15, 26, 92, 259},
        {14, 10, 153, 106, 156, 147, 28, 285, 268, 277, 209, 131, 46, 139, 149, 216, 114, 77, 162, 76, 219, 238, 32, 124},
        {255, 65, 251, 53, 41, 234, 91, 65, 196, 286, 160, 222, 99, 255, 172, 236, 211, 178, 251, 75, 30, 188, 269, 96},
        {148, 42, 6, 119, 104, 117, 28, 54, 96, 191, 152, 36, 142, 182, 50, 43, 27, 21, 58, 142, 53, 4, 111, 259},
        {175, 152, 216, 132, 55, 256, 274, 141, 85, 88, 262, 148, 87, 32, 126, 254, 201, 138, 277, 19, 265, 207, 99, 224},
        {263, 245, 30, 270, 263, 158, 21, 137, 97, 193, 87, 9, 9, 50, 108, 228, 80, 111, 139, 80, 113, 215, 287, 125},
        {232, 212, 215, 20, 63, 245, 112, 41, 195, 82, 253, 40, 192, 66, 192, 67, 67, 7, 162, 5, 63, 176, 176, 40}};

    vector<vector<int>> grid = init_grid;
    n = grid.size();
    locked = vector<vector<int>>(n, vector<int>(n, 0));
    vector<Rotation> full_path;

    // This is a "Frame method" which paired up around toward center
    for (int Fsize = n - cnt * 2; Fsize > n / 2; Fsize -= 4)
    {
        vector<vector<int>> crop;
        vector<Rotation> partial_path;
        crop.reserve(n - cnt * 2);

        for (int i = cnt; i < n - cnt; i++)
        {
            vector<int> row(grid[i].begin() + cnt,
                            grid[i].end() - cnt);
            crop.push_back(row);
        }
        // print_grid(crop);

        pair<vector<vector<int>>, vector<Rotation>> res = STEP_Do(Fsize, crop, 0);
        partial_path.insert(partial_path.end(), res.second.begin(), res.second.end());

        crop = rotate_submatrix(res.first, Fsize, 0, 0);
        locked = rotate_submatrix(locked, Fsize, cnt, cnt);
        partial_path.emplace_back(Fsize, cnt, cnt);

        for (int i = 0; i < 3; i++)
        {
            auto res1 = STEP_Do(Fsize, crop, 0);
            partial_path.insert(partial_path.end(), res1.second.begin(), res1.second.end());

            auto res2 = STEP_Do(Fsize, res1.first, 2);
            partial_path.insert(partial_path.end(), res2.second.begin(), res2.second.end());

            crop = rotate_submatrix(res2.first, Fsize, 0, 0);
            locked = rotate_submatrix(locked, Fsize, cnt, cnt);
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

        // quit = show_prompt(crop);
        // if (quit)
        //     break;

        cnt += 2;
    }

    cout << "-----------------           Before          -------------------\n";
    print_grid(init_grid);
    cout << "-----------------           After           -------------------\n";
    init_grid = apply_rotations(init_grid, full_path);
    print_grid(init_grid);
    cout << "\n ops: " << full_path.size();

    // Next Algorithm
    // Now unsolve grid = (n/2)* (n/2)
    // For the remaining inner grid (12x12 in this case), implement a global beam search

    if (broke)
        cout << "-------------------------------------BROKE----------------------------------\n";
    return 0;
}

//~~~