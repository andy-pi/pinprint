# Pin Printer — How It Works

This project takes a 3D scan of an object (in STL format) and figures out how tall
each pin in a physical pin array needs to be to reproduce the shape of that object.

Think of it like a bed of nails — each nail can be raised or lowered independently.
When all the nails are set to the right heights, the surface they form matches the
shape of the original 3D scan.

---

## The two parts

### Part 1 — C++ program (`stl_to_pins`)

This is the heavy lifting. It reads the STL file and works out a height value for
every pin in the grid.

**What it does, step by step:**

1. **Loads the 3D mesh** — the STL file is a list of millions of tiny triangles that
   together describe the surface of the object.

2. **Reorients the mesh** — the scan may have been saved with a different "up"
   direction, or upside down. The program corrects this so the object is right-side
   up with its base at zero.

3. **Fires a ray for each pin** — for every pin position in the grid, it shoots an
   imaginary vertical line upward and finds where that line hits the surface of the
   object. The height of that hit becomes the pin's target height.

4. **Writes the result** — saves a simple binary file (`pins.bin`) containing one
   height value per pin, arranged in a grid. Heights run from 0 (fully retracted)
   to 100 (fully extended).

**The grid is currently set to 200 × 200 pins.** This can be changed by editing
the two lines near the top of `stl_to_pins.cpp`:

```cpp
const int PIN_GRID_COLS = 200;
const int PIN_GRID_ROWS = 200;
```

**To compile and run:**

```bash
c++ -O2 -std=c++17 -o stl_to_pins stl_to_pins.cpp
./stl_to_pins my_object.stl pins.bin
```

---

### Part 2 — Python visualiser (`visualise_pins.py`)

This reads the `pins.bin` file and draws a 3D bar chart so you can see what the
pin bed would look like. Each bar is one pin, and its height is how far that pin
extends.

The visualiser also applies an **inversion** — because the physical machine works
as a mould, pins that sit under a tall feature of the object are pushed *down*
(short pin), while pins in a gap are left *up* (tall pin).

**To run:**

```bash
uv run --with matplotlib --with numpy visualise_pins.py pins.bin
```

---

## The output file (`pins.bin`)

This is the file a machine controller would read. Its format is deliberately simple:

| Bytes | Content |
|-------|---------|
| 4 | Number of rows (integer) |
| 4 | Number of columns (integer) |
| 4 × rows × cols | Pin heights, left-to-right, top-to-bottom (32-bit floats) |

Each height is a number between **0.0** (pin fully retracted) and **100.0**
(pin fully extended). The value represents a percentage of the pin's total
travel range.

**Reading it in C++ (for a machine controller):**

```cpp
#include <fstream>
#include <vector>
#include <cstdint>

int main() {
    std::ifstream file("pins.bin", std::ios::binary);

    int32_t rows, cols;
    file.read(reinterpret_cast<char*>(&rows), 4);
    file.read(reinterpret_cast<char*>(&cols), 4);

    std::vector<float> heights(rows * cols);
    file.read(reinterpret_cast<char*>(heights.data()), rows * cols * sizeof(float));

    // heights[row * cols + col] gives the target height for the pin at (row, col)
}
```

To convert a height value to a real-world distance, multiply by your pin's
physical travel range. For example, if each pin can travel 20 mm:

```cpp
float travel_mm        = 20.0f;
float actual_height_mm = heights[row * cols + col] / 100.0f * travel_mm;
```

To send all pins to a machine:

```cpp
for (int row = 0; row < rows; ++row) {
    for (int col = 0; col < cols; ++col) {
        int   pin_id     = row * cols + col;
        float pin_height = heights[pin_id];
        // send pin_id and pin_height to your hardware here
    }
}
```
