"""
visualise_pins.py
-----------------
Reads the binary height matrix produced by stl_to_pins and renders it
as a 3D bar plot — one bar per pin, height = pin extension.

Usage:
    uv run --with matplotlib --with numpy visualise_pins.py pins.bin
"""

import sys
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D

# ─── Settings ─────────────────────────────────────────────────────────────────

MAX_BARS_PER_AXIS = 40
PIN_MAX_HEIGHT    = 100

# ─── Load binary matrix ───────────────────────────────────────────────────────

bin_file = sys.argv[1] if len(sys.argv) > 1 else "pins.bin"

with open(bin_file, "rb") as f:
    rows, cols = np.frombuffer(f.read(8), dtype=np.int32)
    heights    = np.frombuffer(f.read(), dtype=np.float32).reshape(rows, cols).copy()

print(f"Loaded {rows}x{cols} pin matrix  (heights {heights.min():.1f} – {heights.max():.1f})")

# ─── Downsample to 40x40 ──────────────────────────────────────────────────────

step_row = max(1, rows // MAX_BARS_PER_AXIS)
step_col = max(1, cols // MAX_BARS_PER_AXIS)

trimmed_rows = (rows // step_row) * step_row
trimmed_cols = (cols // step_col) * step_col

downsampled = heights[:trimmed_rows, :trimmed_cols].reshape(
    trimmed_rows // step_row, step_row,
    trimmed_cols // step_col, step_col
).max(axis=(1, 3))

# Invert after downsampling so tall tooth → short pin (mould view)
downsampled = PIN_MAX_HEIGHT - downsampled

# Cap any pins still at 100 (no-hit outside the mesh) to the highest
# legitimate pin value so they don't dominate the colour scale and Z axis.
non_max_values = downsampled[downsampled < PIN_MAX_HEIGHT]
if non_max_values.size > 0:
    cap = non_max_values.max()
    downsampled = np.clip(downsampled, 0, cap)

display_rows, display_cols = downsampled.shape
print(f"Rendering {display_rows}x{display_cols} bars")

# ─── Build bar positions ──────────────────────────────────────────────────────

bar_x_positions, bar_y_positions = np.meshgrid(
    np.arange(display_cols),
    np.arange(display_rows)
)
bar_x_flat      = bar_x_positions.ravel()
bar_y_flat      = bar_y_positions.ravel()
bar_height_flat = downsampled.ravel()
bar_bottom      = np.zeros_like(bar_height_flat)

colour_norm = bar_height_flat / PIN_MAX_HEIGHT
bar_colours = plt.cm.plasma(colour_norm)

# ─── Plot ─────────────────────────────────────────────────────────────────────

fig = plt.figure(figsize=(16, 7))

ax1 = fig.add_subplot(121, projection="3d")
ax1.bar3d(bar_x_flat, bar_y_flat, bar_bottom,
          0.8, 0.8, bar_height_flat,
          color=bar_colours, shade=True, zsort="average")
ax1.set_xlabel("col (X)")
ax1.set_ylabel("row (Y)")
ax1.set_zlabel("height (Z)")
ax1.set_zlim(0, PIN_MAX_HEIGHT)
ax1.set_title("3D view")
ax1.view_init(elev=30, azim=120)

ax2 = fig.add_subplot(122, projection="3d")
ax2.bar3d(bar_x_flat, bar_y_flat, bar_bottom,
          0.8, 0.8, bar_height_flat,
          color=bar_colours, shade=True, zsort="average")
ax2.set_xlabel("col (X)")
ax2.set_ylabel("row (Y)")
ax2.set_zlabel("height (Z)")
ax2.set_zlim(0, PIN_MAX_HEIGHT)
ax2.set_title("Top-down view")
ax2.view_init(elev=90, azim=-90)

plt.tight_layout()
plt.savefig("pins_3d.png", dpi=120)
print("Saved pins_3d.png")
plt.show()
