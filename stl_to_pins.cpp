/*
    stl_to_pins.cpp
    ---------------
    Reads a binary STL file and computes how tall each pin in a grid should be
    to reproduce that object's surface shape.

    For each pin at (x, y), a vertical ray is fired upward. The highest point
    where that ray hits the mesh becomes the pin height, normalised to 0-100.

    Output binary format:
        int32   rows
        int32   cols
        float32[rows * cols]   heights, row-major

    Compile:  c++ -O2 -std=c++17 -o stl_to_pins stl_to_pins.cpp
    Run:      ./stl_to_pins input.stl output.bin
*/

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <limits>
#include <algorithm>

// ─── Constants ────────────────────────────────────────────────────────────────

const int   PIN_GRID_COLS     = 200;
const int   PIN_GRID_ROWS     = 200;
const float PIN_MAX_HEIGHT    = 100.0f;
const int   SPATIAL_GRID_SIZE = 50;
const float NO_HIT            = -1.0f;
const float EPSILON           = 1e-6f;

// ─── Data types ───────────────────────────────────────────────────────────────

struct Vec3 {
    float x, y, z;
};

struct Triangle {
    Vec3 vertex_a, vertex_b, vertex_c;
};

struct MeshBounds {
    float min_x, min_y, min_z;
    float max_x, max_y, max_z;
};

struct SpatialGrid {
    int   grid_size;
    float cell_width, cell_height;
    float origin_x, origin_y;
    std::vector<std::vector<int>> cells;
};

// ─── Load binary STL ──────────────────────────────────────────────────────────
// Format: 80-byte header, uint32 triangle count, then per triangle:
//   12 bytes normal (ignored), 36 bytes vertices (3 × Vec3), 2 bytes attribute (ignored)

std::vector<Triangle> load_stl(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: cannot open " << file_path << "\n";
        std::exit(1);
    }

    file.seekg(80);
    uint32_t triangle_count = 0;
    file.read(reinterpret_cast<char*>(&triangle_count), 4);
    std::cout << "Loading " << triangle_count << " triangles...\n";

    std::vector<Triangle> triangles(triangle_count);
    for (uint32_t i = 0; i < triangle_count; ++i) {
        float ignored_normal[3];
        file.read(reinterpret_cast<char*>(ignored_normal), 12);
        file.read(reinterpret_cast<char*>(&triangles[i].vertex_a), 12);
        file.read(reinterpret_cast<char*>(&triangles[i].vertex_b), 12);
        file.read(reinterpret_cast<char*>(&triangles[i].vertex_c), 12);
        uint16_t ignored_attr;
        file.read(reinterpret_cast<char*>(&ignored_attr), 2);
    }

    std::cout << "Loaded " << triangle_count << " triangles.\n";
    return triangles;
}

// ─── Reorient mesh ────────────────────────────────────────────────────────────
// This STL uses Y as the vertical axis. Swap Y↔Z so the pipeline,
// which ray-casts along Z, works correctly.

void swap_y_and_z(std::vector<Triangle>& triangles) {
    for (auto& tri : triangles) {
        for (Vec3* v : {&tri.vertex_a, &tri.vertex_b, &tri.vertex_c}) {
            std::swap(v->y, v->z);
        }
    }
    std::cout << "Swapped Y and Z axes.\n";
}

// The STL base is at the top after the swap. Flip Z so the flat base sits at
// Z=0 and the cusps point upward.

void flip_mesh_upright(std::vector<Triangle>& triangles) {
    float z_max = -std::numeric_limits<float>::max();
    for (const auto& tri : triangles)
        z_max = std::max({z_max, tri.vertex_a.z, tri.vertex_b.z, tri.vertex_c.z});

    for (auto& tri : triangles) {
        tri.vertex_a.z = z_max - tri.vertex_a.z;
        tri.vertex_b.z = z_max - tri.vertex_b.z;
        tri.vertex_c.z = z_max - tri.vertex_c.z;
    }
    std::cout << "Flipped mesh upright.\n";
}

// ─── Mesh bounds ──────────────────────────────────────────────────────────────

MeshBounds compute_bounds(const std::vector<Triangle>& triangles) {
    MeshBounds b = {
         std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),
        -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()
    };
    for (const auto& tri : triangles) {
        for (const auto& v : {tri.vertex_a, tri.vertex_b, tri.vertex_c}) {
            b.min_x = std::min(b.min_x, v.x);  b.max_x = std::max(b.max_x, v.x);
            b.min_y = std::min(b.min_y, v.y);  b.max_y = std::max(b.max_y, v.y);
            b.min_z = std::min(b.min_z, v.z);  b.max_z = std::max(b.max_z, v.z);
        }
    }
    return b;
}

// ─── Spatial acceleration grid ────────────────────────────────────────────────
// Divides the XY plane into buckets. Each triangle is stored in every bucket
// its XY footprint overlaps, so each ray only tests a small subset of triangles.

SpatialGrid build_spatial_grid(const std::vector<Triangle>& triangles,
                                const MeshBounds& bounds) {
    SpatialGrid grid;
    grid.grid_size   = SPATIAL_GRID_SIZE;
    grid.origin_x    = bounds.min_x;
    grid.origin_y    = bounds.min_y;
    grid.cell_width  = (bounds.max_x - bounds.min_x) / SPATIAL_GRID_SIZE;
    grid.cell_height = (bounds.max_y - bounds.min_y) / SPATIAL_GRID_SIZE;
    grid.cells.resize(SPATIAL_GRID_SIZE * SPATIAL_GRID_SIZE);

    for (int i = 0; i < (int)triangles.size(); ++i) {
        const Triangle& tri = triangles[i];
        float tri_min_x = std::min({tri.vertex_a.x, tri.vertex_b.x, tri.vertex_c.x});
        float tri_max_x = std::max({tri.vertex_a.x, tri.vertex_b.x, tri.vertex_c.x});
        float tri_min_y = std::min({tri.vertex_a.y, tri.vertex_b.y, tri.vertex_c.y});
        float tri_max_y = std::max({tri.vertex_a.y, tri.vertex_b.y, tri.vertex_c.y});

        int col_start = std::max(0, std::min((int)((tri_min_x - grid.origin_x) / grid.cell_width),  SPATIAL_GRID_SIZE - 1));
        int col_end   = std::max(0, std::min((int)((tri_max_x - grid.origin_x) / grid.cell_width),  SPATIAL_GRID_SIZE - 1));
        int row_start = std::max(0, std::min((int)((tri_min_y - grid.origin_y) / grid.cell_height), SPATIAL_GRID_SIZE - 1));
        int row_end   = std::max(0, std::min((int)((tri_max_y - grid.origin_y) / grid.cell_height), SPATIAL_GRID_SIZE - 1));

        for (int row = row_start; row <= row_end; ++row)
            for (int col = col_start; col <= col_end; ++col)
                grid.cells[row * SPATIAL_GRID_SIZE + col].push_back(i);
    }

    std::cout << "Built " << SPATIAL_GRID_SIZE << "x" << SPATIAL_GRID_SIZE << " spatial grid.\n";
    return grid;
}

// ─── Möller–Trumbore ray-triangle intersection ────────────────────────────────
// Fires a vertical ray upward from (ray_x, ray_y). Returns the Z of the hit,
// or NO_HIT if the ray misses the triangle.
// Ray direction is (0, 0, 1), which simplifies the standard algorithm.

float ray_hits_triangle_at_z(float ray_x, float ray_y, const Triangle& tri) {
    float edge1_x = tri.vertex_b.x - tri.vertex_a.x;
    float edge1_y = tri.vertex_b.y - tri.vertex_a.y;
    float edge1_z = tri.vertex_b.z - tri.vertex_a.z;

    float edge2_x = tri.vertex_c.x - tri.vertex_a.x;
    float edge2_y = tri.vertex_c.y - tri.vertex_a.y;
    float edge2_z = tri.vertex_c.z - tri.vertex_a.z;

    // h = ray_dir × edge2, with ray_dir = (0, 0, 1) this gives (-edge2_y, edge2_x, 0)
    float h_x = -edge2_y;
    float h_y =  edge2_x;

    float determinant = edge1_x * h_x + edge1_y * h_y;
    if (determinant > -EPSILON && determinant < EPSILON) return NO_HIT;

    float inv_det  = 1.0f / determinant;
    float to_ray_x = ray_x - tri.vertex_a.x;
    float to_ray_y = ray_y - tri.vertex_a.y;

    float u = (to_ray_x * h_x + to_ray_y * h_y) * inv_det;
    if (u < 0.0f || u > 1.0f) return NO_HIT;

    float q_z = to_ray_x * edge1_y - to_ray_y * edge1_x;
    float v   = q_z * inv_det;
    if (v < 0.0f || u + v > 1.0f) return NO_HIT;

    float t = edge2_z * q_z * inv_det;
    if (t < 0.0f) return NO_HIT;

    return tri.vertex_a.z + u * edge1_z + v * edge2_z;
}

// ─── Cast all pin rays ────────────────────────────────────────────────────────

std::vector<float> compute_pin_heights(const std::vector<Triangle>& triangles,
                                        const SpatialGrid& grid,
                                        const MeshBounds& bounds) {
    const int total_pins      = PIN_GRID_COLS * PIN_GRID_ROWS;
    const float spacing_x     = (bounds.max_x - bounds.min_x) / (PIN_GRID_COLS - 1);
    const float spacing_y     = (bounds.max_y - bounds.min_y) / (PIN_GRID_ROWS - 1);
    const float height_range  = bounds.max_z - bounds.min_z;

    std::vector<float> pin_heights(total_pins, 0.0f);

    for (int pin_row = 0; pin_row < PIN_GRID_ROWS; ++pin_row) {
        for (int pin_col = 0; pin_col < PIN_GRID_COLS; ++pin_col) {

            float world_x = bounds.min_x + pin_col * spacing_x;
            float world_y = bounds.min_y + pin_row * spacing_y;

            int grid_col = std::max(0, std::min((int)((world_x - grid.origin_x) / grid.cell_width),  SPATIAL_GRID_SIZE - 1));
            int grid_row = std::max(0, std::min((int)((world_y - grid.origin_y) / grid.cell_height), SPATIAL_GRID_SIZE - 1));

            float highest_hit_z  = -std::numeric_limits<float>::max();
            bool  ray_hit_anything = false;

            for (int tri_index : grid.cells[grid_row * SPATIAL_GRID_SIZE + grid_col]) {
                float hit_z = ray_hits_triangle_at_z(world_x, world_y, triangles[tri_index]);
                if (hit_z != NO_HIT) {
                    highest_hit_z   = std::max(highest_hit_z, hit_z);
                    ray_hit_anything = true;
                }
            }

            if (ray_hit_anything) {
                float surface_fraction = (highest_hit_z - bounds.min_z) / height_range;
                pin_heights[pin_row * PIN_GRID_COLS + pin_col] =
                    std::max(0.0f, std::min(surface_fraction * PIN_MAX_HEIGHT, PIN_MAX_HEIGHT));
            }
        }

        if ((pin_row + 1) % 10 == 0)
            std::cout << "  " << (pin_row + 1) * PIN_GRID_COLS << " / " << total_pins << " pins computed\n";
    }

    return pin_heights;
}

// ─── Write binary output ──────────────────────────────────────────────────────

void save_height_matrix(const std::string& output_path,
                        const std::vector<float>& pin_heights,
                        int rows, int cols) {
    std::ofstream file(output_path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: cannot write " << output_path << "\n";
        std::exit(1);
    }
    int32_t r = rows, c = cols;
    file.write(reinterpret_cast<const char*>(&r), 4);
    file.write(reinterpret_cast<const char*>(&c), 4);
    file.write(reinterpret_cast<const char*>(pin_heights.data()), pin_heights.size() * sizeof(float));
    std::cout << "Saved " << rows << "x" << cols << " height matrix to " << output_path << "\n";
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Usage: stl_to_pins <input.stl> <output.bin>\n";
        return 1;
    }

    std::vector<Triangle> triangles = load_stl(argv[1]);
    swap_y_and_z(triangles);
    flip_mesh_upright(triangles);

    MeshBounds bounds = compute_bounds(triangles);
    std::cout << "Bounds: X " << bounds.min_x << " to " << bounds.max_x
              << "  Y " << bounds.min_y << " to " << bounds.max_y
              << "  Z " << bounds.min_z << " to " << bounds.max_z << " mm\n";

    SpatialGrid grid = build_spatial_grid(triangles, bounds);

    std::cout << "Computing pin heights (" << PIN_GRID_COLS << "x" << PIN_GRID_ROWS << ")...\n";
    std::vector<float> pin_heights = compute_pin_heights(triangles, grid, bounds);

    save_height_matrix(argv[2], pin_heights, PIN_GRID_ROWS, PIN_GRID_COLS);
    std::cout << "Done.\n";
    return 0;
}
