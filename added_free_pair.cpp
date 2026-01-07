
#include <bits/stdc++.h>
#include <fstream>
using namespace std;

struct Rotation {
    int k, i, j;
    Rotation(int k, int i, int j) : k(k), i(i), j(j) {}
};

vector<vector<int>> locked;
int n;
int cnt = 0;
int side = 0;

// Utility functions
int count_adjacent_pairs(const vector<vector<int>> &grid) {
    int count = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if (i + 1 < n && grid[i][j] == grid[i + 1][j]) count++;
            if (j + 1 < n && grid[i][j] == grid[i][j + 1]) count++;
        }
    }
    return count;
}

vector<vector<int>> rotate_submatrix(const vector<vector<int>> &grid, int k, int i, int j) {
    vector<vector<int>> new_grid = grid;
    for (int x = 0; x < k; ++x) {
        for (int y = 0; y < k; ++y) {
            new_grid[i + y][j + k - 1 - x] = grid[i + x][j + y];
        }
    }
    return new_grid;
}

void print_grid(const vector<vector<int>> &grid) {
    for (const auto &row : grid) {
        for (int val : row) {
            cout << setw(3) << val << " ";
        }
        cout << endl;
    }
}

vector<vector<int>> apply_rotations(vector<vector<int>> grid, const vector<Rotation> &path) {
    for (const auto &rot : path) {
        grid = rotate_submatrix(grid, rot.k, rot.i, rot.j);
    }
    return grid;
}

bool Check_Valid(int i, int j, int k) {
    if (locked[i][j] || locked[i + k][j + k] || locked[i][j + k] || locked[i + k][j])
        return false;
    for (int ft = 2; ft < k; ft += 2) {
        if (locked[i + ft][j] || locked[i][j + ft] || locked[i + ft][j + k] || locked[i + k][j + ft])
            return false;
    }
    return true;
}

string serialize(const vector<vector<int>> &g) {
    string s;
    int N = g.size();
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            s += to_string(g[r][c]) + ",";
        }
    }
    return s;
}

struct State {
    vector<vector<int>> grid;
    vector<Rotation> path;
};

// Find all existing pairs in the grid (excluding locked cells)
vector<tuple<int, int, int, int, int>> find_free_pairs(const vector<vector<int>> &grid) {
    vector<tuple<int, int, int, int, int>> pairs; // r1, c1, r2, c2, value
    int N = grid.size();
    
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            if (locked[cnt + i][cnt + j]) continue;
            
            // Check horizontal pair
            if (j + 1 < N && !locked[cnt + i][cnt + j + 1] && grid[i][j] == grid[i][j + 1]) {
                pairs.push_back({i, j, i, j + 1, grid[i][j]});
            }
            // Check vertical pair (avoid duplicates)
            if (i + 1 < N && !locked[cnt + i + 1][cnt + j] && grid[i][j] == grid[i + 1][j]) {
                pairs.push_back({i, j, i + 1, j, grid[i][j]});
            }
        }
    }
    return pairs;
}

// Beam search to move a free pair to target position
pair<int, vector<Rotation>> move_free_pair_to_target(const vector<vector<int>> &initial_grid, 
                                                      int pr1, int pc1, int pr2, int pc2,
                                                      int target_r, int target_c) {
    const int beam_width = 10;
    const int max_depth = 5; // Shorter depth for moving existing pairs
    int N = initial_grid.size();
    unordered_set<string> visited;
    vector<State> current_beam = {{initial_grid, {}}};
    visited.insert(serialize(initial_grid));

    for (int depth = 0; depth < max_depth; ++depth) {
        vector<State> next_beam;
        for (const auto &cur : current_beam) {
            // Check if the pair is now at target position (horizontal)
            if (cur.grid[target_r][target_c] == cur.grid[target_r][target_c + 1] &&
                cur.grid[target_r][target_c] == initial_grid[pr1][pc1]) {
                return {static_cast<int>(cur.path.size()), cur.path};
            }
            
            for (int k = 2; k <= N - 1; ++k) {
                for (int r = 0; r <= N - k; ++r) {
                    for (int c = 0; c <= N - k; ++c) {
                        if (!Check_Valid(r + cnt, c + cnt, k - 1))
                            continue;
                        
                        // Only consider rotations that affect the pair or target
                        bool affects = false;
                        if (r <= pr1 && pr1 < r + k && c <= pc1 && pc1 < c + k) affects = true;
                        if (r <= pr2 && pr2 < r + k && c <= pc2 && pc2 < c + k) affects = true;
                        if (r <= target_r && target_r < r + k && c <= target_c && target_c < c + k) affects = true;
                        if (!affects) continue;
                        
                        vector<vector<int>> new_grid = rotate_submatrix(cur.grid, k, r, c);
                        string ser = serialize(new_grid);

                        if (visited.find(ser) == visited.end()) {
                            visited.insert(ser);
                            vector<Rotation> new_path = cur.path;
                            new_path.emplace_back(k, r, c);
                            next_beam.push_back({new_grid, new_path});
                        }
                    }
                }
            }
        }
        
        // Sort by heuristic
        auto heuristic = [&](const State &st) {
            int dist = abs(st.grid[target_r][target_c] - initial_grid[pr1][pc1]);
            return dist + st.path.size();
        };
        sort(next_beam.begin(), next_beam.end(),
             [&](const State &a, const State &b) {
                 return heuristic(a) < heuristic(b);
             });
        
        if (next_beam.size() > static_cast<size_t>(beam_width)) {
            next_beam.resize(beam_width);
        }
        current_beam = std::move(next_beam);
        if (current_beam.empty()) break;
    }
    
    // Check final beam
    for (const auto &cur : current_beam) {
        if (cur.grid[target_r][target_c] == cur.grid[target_r][target_c + 1] &&
            cur.grid[target_r][target_c] == initial_grid[pr1][pc1]) {
            return {static_cast<int>(cur.path.size()), cur.path};
        }
    }
    return {999, {}};
}

// Original beam search
pair<int, vector<Rotation>> search_pair(const vector<vector<int>> &initial_grid, int row1, int col1, int row2, int col2) {
    const int beam_width = 10;
    const int max_depth = 10;
    int N = initial_grid.size();
    unordered_set<string> visited;
    vector<State> current_beam = {{initial_grid, {}}};
    visited.insert(serialize(initial_grid));

    for (int depth = 0; depth < max_depth; ++depth) {
        vector<State> next_beam;
        for (const auto &cur : current_beam) {
            if (cur.grid[row1][col1] == cur.grid[row2][col2]) {
                return {static_cast<int>(cur.path.size()), cur.path};
            }
            for (int k = 2; k <= N - 1; ++k) {
                for (int r = 0; r <= N - k; ++r) {
                    for (int c = 0; c <= N - k; ++c) {
                        if (!Check_Valid(r + cnt, c + cnt, k - 1))
                            continue;
                        
                        bool affects = false;
                        if (r <= row1 && row1 < r + k && c <= col1 && col1 < c + k)
                            affects = true;
                        if (r <= row2 && row2 < r + k && c <= col2 && col2 < c + k)
                            affects = true;
                        if (!affects) continue;
                        
                        vector<vector<int>> new_grid = rotate_submatrix(cur.grid, k, r, c);
                        string ser = serialize(new_grid);

                        if (visited.find(ser) == visited.end()) {
                            visited.insert(ser);
                            vector<Rotation> new_path = cur.path;
                            new_path.emplace_back(k, r, c);
                            next_beam.push_back({new_grid, new_path});
                        }
                    }
                }
            }
        }
        auto heuristic = [&](const State &st) {
            return abs(st.grid[row1][col1] - st.grid[row2][col2]) + st.path.size();
        };
        sort(next_beam.begin(), next_beam.end(),
             [&](const State &a, const State &b) {
                 return heuristic(a) < heuristic(b);
             });
        
        if (next_beam.size() > static_cast<size_t>(beam_width)) {
            next_beam.resize(beam_width);
        }
        current_beam = std::move(next_beam);
        if (current_beam.empty()) break;
    }
    
    for (const auto &cur : current_beam) {
        if (cur.grid[row1][col1] == cur.grid[row2][col2]) {
            return {static_cast<int>(cur.path.size()), cur.path};
        }
    }
    return {999, {}};
}

vector<Rotation> Do_step(vector<vector<int>> grid, int i) {
    int min_ops = 999;
    vector<Rotation> result;
    
    // First check for free pairs that can be moved to (i, 0)-(i, 1)
    auto free_pairs = find_free_pairs(grid);
    cout << "Found " << free_pairs.size() << " free pairs. ";
    
    for (const auto& [pr1, pc1, pr2, pc2, val] : free_pairs) {
        auto move_result = move_free_pair_to_target(grid, pr1, pc1, pr2, pc2, i, 0);
        if (move_result.first < min_ops) {
            result = move_result.second;
            min_ops = move_result.first;
            cout << "Using free pair (" << pr1 << "," << pc1 << ")-(" << pr2 << "," << pc2 << ") with cost " << min_ops << ". ";
        }
    }
    
    // Method 1: place horizontally at (i, 0)-(i, 1)
    pair<int, vector<Rotation>> temp_h = search_pair(grid, i, 0, i, 1);
    if (temp_h.first < min_ops) {
        result = temp_h.second;
        min_ops = temp_h.first;
    }
    
    // Method 2: place vertically then rotate
    int max_step = grid.size();
    if (side != 0) {
        if (i < 2)
            max_step /= 2;
        else
            max_step -= 2;
    }
    max_step--;
    
    for (int step = i; step < max_step; step++) {
        pair<int, vector<Rotation>> temp_d = search_pair(grid, step, 0, step + 1, 0);
        if (temp_d.first + 1 < min_ops) {
            result = temp_d.second;
            result.emplace_back(step - i + 2, i, 0);
            min_ops = temp_d.first + 1;
        }
    }
    
    cout << "Final cost: " << min_ops << '\n';
    return result;
}

pair<vector<vector<int>>, vector<Rotation>> Do_side_other(int Fsize, vector<vector<int>> grid) {
    vector<Rotation> partial_path;
    
    for (side = 1; side < 4; side++) {
        for (int step = 0; step < 2; step++) {
            cout << "Fsize: " << Fsize << ", side: " << side + 1 << ", step: " << ((step) ? 3 : 1) << "\n";
            
            for (int i = step * 2; i < Fsize / 2; i += 1) {
                vector<Rotation> partial_result = Do_step(grid, i);
                
                grid = apply_rotations(grid, partial_result);
                for (auto &rot : partial_result) {
                    partial_path.emplace_back(rot.k, rot.i + cnt, rot.j + cnt);
                }
                
                locked[cnt + i][cnt + 1] = 1;
                locked[cnt + i][cnt] = 1;
            }
            
            if (step == 1) break;
            
            locked = rotate_submatrix(locked, Fsize / 2, cnt, cnt);
            grid = rotate_submatrix(grid, Fsize / 2, 0, 0);
            partial_path.emplace_back(Fsize / 2, cnt, cnt);
        }
        
        locked = rotate_submatrix(locked, Fsize, cnt, cnt);
        grid = rotate_submatrix(grid, Fsize, 0, 0);
        partial_path.emplace_back(Fsize, cnt, cnt);
        
        cout << "Completed side " << side + 1 << ", total ops: " << partial_path.size() << '\n';
    }
    
    return make_pair(grid, partial_path);
}

pair<vector<vector<int>>, vector<Rotation>> Do_side_1_bottom(int Fsize, vector<vector<int>> grid) {
    vector<Rotation> result;
    int half = Fsize / 2;
    int row = half - 2;
    
    cout << "Fsize: " << Fsize << ", side: 1, step: 3 (bottom center)\n";
    for (int j = 2; j < half; j += 1) {
        int min_ops = 999;
        vector<Rotation> partial_result;
        
        for (int step = row + 1; step < Fsize - 2; step++) {
            pair<int, vector<Rotation>> temp_d = search_pair(grid, step, j, step, j + 1);
            if (temp_d.first + 1 < min_ops) {
                partial_result = temp_d.second;
                partial_result.emplace_back(step - row + 1, row, j);
                min_ops = temp_d.first + 1;
            }
        }
        
        grid = apply_rotations(grid, partial_result);
        for (auto &rot : partial_result) {
            result.emplace_back(rot.k, rot.i + cnt, rot.j + cnt);
        }
        
        cout << "Cost: " << min_ops << '\n';
        locked[cnt + row][cnt + j] = 1;
        locked[cnt + row + 1][cnt + j] = 1;
    }
    
    locked = rotate_submatrix(locked, half, cnt, cnt);
    grid = rotate_submatrix(grid, half, 0, 0);
    result.emplace_back(half, cnt, cnt);
    
    return make_pair(grid, result);
}

void save_to_json(const vector<vector<int>>& initial_grid, const vector<Rotation>& path, string filename) {
    ofstream file(filename);
    file << "{\n";
    
    file << "  \"initialGrid\": [\n";
    for (int i = 0; i < initial_grid.size(); ++i) {
        file << "    [";
        for (int j = 0; j < initial_grid[i].size(); ++j) {
            file << initial_grid[i][j] << (j == initial_grid[i].size() - 1 ? "" : ", ");
        }
        file << "]" << (i == initial_grid.size() - 1 ? "" : ",\n");
    }
    file << "  ],\n";

    file << "  \"path\": [\n";
    for (int i = 0; i < path.size(); ++i) {
        file << "    {\"k\": " << path[i].k << ", \"i\": " << path[i].i << ", \"j\": " << path[i].j << "}"
             << (i == path.size() - 1 ? "" : ",\n");
    }
    file << "  ]\n";
    file << "}";
    file.close();
}

int main() {
    vector<vector<int>> init_grid = {
        {247, 156, 157, 189, 100, 40, 4, 199, 229, 9, 281, 194, 40, 226, 259, 7, 52, 18, 274, 245, 24, 60, 19, 184},
        {162, 6, 161, 235, 17, 224, 157, 59, 173, 133, 257, 110, 181, 166, 274, 165, 89, 69, 213, 158, 170, 127, 259, 189},
        {175, 78, 191, 235, 25, 110, 104, 255, 24, 286, 266, 253, 271, 105, 58, 252, 38, 102, 33, 141, 95, 284, 280, 120},
        {107, 266, 118, 73, 30, 70, 6, 203, 256, 197, 28, 19, 5, 155, 233, 1, 127, 226, 205, 162, 83, 0, 77, 186},
        {42, 165, 37, 64, 168, 242, 95, 66, 160, 133, 139, 172, 63, 51, 250, 176, 30, 145, 181, 262, 107, 287, 261, 192},
        {252, 195, 5, 141, 190, 60, 161, 106, 244, 113, 90, 272, 47, 70, 79, 137, 77, 123, 18, 275, 49, 66, 69, 183},
        {136, 114, 83, 193, 82, 64, 136, 205, 56, 278, 234, 211, 255, 151, 72, 188, 221, 239, 201, 277, 117, 167, 280, 14},
        {249, 163, 88, 137, 20, 204, 237, 279, 211, 9, 236, 264, 213, 75, 103, 134, 277, 55, 178, 14, 193, 134, 42, 152},
        {204, 43, 0, 208, 229, 173, 79, 199, 149, 217, 218, 263, 282, 43, 53, 247, 228, 111, 264, 128, 65, 272, 192, 39},
        {221, 7, 182, 178, 152, 81, 76, 188, 145, 115, 89, 219, 214, 238, 187, 90, 81, 117, 120, 194, 140, 240, 160, 12},
        {140, 287, 208, 15, 16, 219, 84, 121, 13, 88, 126, 101, 198, 22, 67, 84, 210, 86, 261, 186, 130, 258, 44, 33},
        {249, 209, 154, 206, 256, 52, 174, 241, 41, 198, 183, 105, 129, 258, 63, 102, 56, 48, 240, 168, 263, 2, 201, 73},
        {62, 51, 3, 156, 172, 98, 128, 180, 93, 267, 241, 98, 279, 177, 2, 10, 271, 45, 4, 233, 154, 46, 260, 285},
        {270, 142, 151, 230, 38, 78, 174, 265, 86, 222, 20, 129, 123, 282, 278, 108, 80, 166, 3, 119, 185, 45, 57, 75},
        {227, 61, 99, 239, 238, 254, 54, 242, 283, 114, 149, 202, 61, 92, 283, 39, 32, 15, 150, 59, 142, 32, 23, 26},
        {58, 10, 251, 200, 281, 44, 93, 94, 253, 31, 34, 250, 138, 131, 236, 115, 286, 74, 135, 100, 270, 67, 11, 245},
        {68, 177, 265, 155, 217, 8, 231, 231, 99, 220, 207, 244, 41, 25, 53, 275, 269, 104, 243, 37, 112, 109, 72, 260},
        {147, 138, 76, 62, 82, 49, 92, 214, 175, 113, 126, 8, 163, 195, 97, 36, 112, 55, 28, 284, 254, 96, 215, 159},
        {180, 243, 164, 48, 108, 179, 17, 50, 269, 12, 191, 68, 232, 74, 182, 196, 91, 87, 71, 103, 94, 251, 11, 101},
        {285, 169, 176, 232, 215, 46, 222, 147, 164, 167, 196, 87, 159, 225, 171, 131, 246, 185, 96, 1, 268, 130, 179, 29},
        {262, 187, 119, 169, 206, 212, 80, 202, 144, 106, 153, 212, 111, 36, 228, 257, 218, 234, 197, 26, 230, 97, 153, 47},
        {85, 109, 170, 237, 135, 146, 190, 54, 21, 227, 50, 276, 35, 184, 171, 248, 209, 122, 216, 210, 31, 216, 200, 34},
        {65, 148, 71, 125, 144, 267, 132, 224, 85, 150, 122, 29, 21, 91, 22, 225, 35, 27, 13, 220, 27, 158, 268, 207},
        {276, 132, 146, 23, 246, 273, 273, 118, 148, 125, 116, 139, 16, 124, 124, 116, 248, 121, 203, 57, 223, 143, 143, 223}
    };
    
    vector<vector<int>> init_copy = init_grid;
    n = init_grid.size();
    locked = vector<vector<int>>(n, vector<int>(n, 0));
    vector<Rotation> full_path;
    
    // Frame-by-frame border solving - STOP AT 12x12
    for (int Fsize = n - cnt * 2; Fsize > 12; Fsize -= 4) {
        cout << "\n========== Processing Frame Size: " << Fsize << " ==========\n";
        
        vector<vector<int>> crop;
        vector<Rotation> partial_path;
        crop.reserve(n - cnt * 2);
        
        for (int i = cnt; i < n - cnt; i++) {
            vector<int> row(init_grid[i].begin() + cnt, init_grid[i].end() - cnt);
            crop.push_back(row);
        }
        print_grid(crop);
        
        side = 0;
        // Side 1 - Step 1
        cout << "\nFsize: " << Fsize << ", side: 1, step: 1\n";
        for (int i = 0; i < Fsize / 2; i += 1) {
            vector<Rotation> step_path = Do_step(crop, i);
            crop = apply_rotations(crop, step_path);
            for (auto &rot : step_path) {
                partial_path.emplace_back(rot.k, rot.i + cnt, rot.j + cnt);
            }
            locked[cnt + i][cnt] = 1;
            locked[cnt + i][cnt + 1] = 1;
        }
        
        // Side 1 - Step 2
        cout << "Fsize: " << Fsize << ", side: 1, step: 2 (rotate full frame)\n";
        locked = rotate_submatrix(locked, Fsize, cnt, cnt);
        crop = rotate_submatrix(crop, Fsize, 0, 0);
        partial_path.emplace_back(Fsize, cnt, cnt);
        
        // Sides 2, 3, 4
        auto result_other = Do_side_other(Fsize, crop);
        crop = result_other.first;
        partial_path.insert(partial_path.end(), result_other.second.begin(), result_other.second.end());
        
        // Side 1 - Step 3
        auto result_bottom = Do_side_1_bottom(Fsize, crop);
        crop = result_bottom.first;
        partial_path.insert(partial_path.end(), result_bottom.second.begin(), result_bottom.second.end());
        
        init_grid = apply_rotations(init_grid, partial_path);
        full_path.insert(full_path.end(), partial_path.begin(), partial_path.end());
        
        cout << "\n>>> Frame " << Fsize << " completed. Total ops this frame: " << partial_path.size() << "\n";
        cout << ">>> Cumulative total ops: " << full_path.size() << "\n";
        cout << "------------------------------------------------------------\n";
        
        cnt += 2;
    }
    
    cout << "\n================ FINAL RESULTS ================\n";
    cout << "BEFORE:\n";
    print_grid(init_copy);
    cout << "\nAFTER:\n";
    init_grid = apply_rotations(init_copy, full_path);
    print_grid(init_grid);
    cout << "\nTotal operations: " << full_path.size() << "\n";
    cout << "Adjacent pairs: " << count_adjacent_pairs(init_grid) << "\n";
    
    save_to_json(init_copy, full_path, "solution.json");
    
    return 0;
}
