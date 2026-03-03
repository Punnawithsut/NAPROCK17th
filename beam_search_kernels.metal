#include <metal_stdlib>
using namespace metal;

struct Rotation {
    int k;
    int i;
    int j;
};

struct GPUResult {
    int paired_count;
    int heuristic;
    uint64_t hash;
    int parent_idx;
    int rotation_idx;
};

constant int ZOBRIST_MAX_N = 64;
constant int ZOBRIST_MAX_VAL = 2048;

kernel void evaluate_rotations(
    device const uint16_t* grids [[buffer(0)]],
    device const Rotation* rotations [[buffer(1)]],
    device const uint64_t* zobrist_table [[buffer(2)]],
    device GPUResult* results [[buffer(3)]],
    constant int& grid_size [[buffer(4)]],
    constant int& num_states [[buffer(5)]],
    constant int& num_rotations [[buffer(6)]],
    uint id [[thread_position_in_grid]]
) {
    if (id >= (uint)(num_states * num_rotations)) return;

    int state_idx = id / num_rotations;
    int rot_idx = id % num_rotations;

    device const uint16_t* original_grid = grids + (state_idx * grid_size * grid_size);
    Rotation rot = rotations[rot_idx];

    // Read to thread-local memory
    uint16_t local_grid[4096]; 
    for(int r = 0; r < grid_size; r++) {
        for(int c = 0; c < grid_size; c++) {
            local_grid[r * grid_size + c] = original_grid[r * grid_size + c];
        }
    }

    // Apply rotation
    int k = rot.k;
    int i = rot.i;
    int j = rot.j;
    
    uint16_t temp[4096]; 
    for(int x = 0; x < k; x++) {
        for(int y = 0; y < k; y++) {
            temp[y * k + (k - 1 - x)] = local_grid[(i + x) * grid_size + (j + y)];
        }
    }
    
    for(int x = 0; x < k; x++) {
        for(int y = 0; y < k; y++) {
            local_grid[(i + x) * grid_size + (j + y)] = temp[x * k + y];
        }
    }

    // Accumulate values
    int max_val = 0;
    uint64_t hash = 0;
    for(int r = 0; r < grid_size; r++) {
        for(int c = 0; c < grid_size; c++) {
            int val = local_grid[r * grid_size + c];
            if (val > max_val) max_val = val;
            
            int z_idx = (r * ZOBRIST_MAX_N * ZOBRIST_MAX_VAL) + (c * ZOBRIST_MAX_VAL) + val;
            hash ^= zobrist_table[z_idx];
        }
    }

    // Pair count logic 
    struct Coord { int r; int c; };
    Coord coords1[2048];
    Coord coords2[2048];
    int coord_counts[2048];
    
    for(int v = 0; v <= max_val; ++v) {
        coord_counts[v] = 0;
    }
    
    for(int r = 0; r < grid_size; r++) {
        for(int c = 0; c < grid_size; c++) {
            int val = local_grid[r * grid_size + c];
            if (coord_counts[val] == 0) {
                coords1[val].r = r;
                coords1[val].c = c;
                coord_counts[val] = 1;
            } else {
                coords2[val].r = r;
                coords2[val].c = c;
                coord_counts[val] = 2;
            }
        }
    }
    
    int paired_count = 0;
    int total_dist = 0;
    
    for(int v = 0; v <= max_val; ++v) {
        if (coord_counts[v] == 2) {
            Coord p1 = coords1[v];
            Coord p2 = coords2[v];
            
            // Standard puzzle distance metric
            int dr = abs(p1.r - p2.r);
            int dc = abs(p1.c - p2.c);
            int dist = dr + dc;
            
            if (dist == 1) {
                paired_count++;
            }
            
            total_dist += dist + max(dr, dc);
        }
    }

    results[id].paired_count = paired_count;
    results[id].heuristic = total_dist;
    results[id].hash = hash;
    results[id].parent_idx = state_idx;
    results[id].rotation_idx = rot_idx;
}
