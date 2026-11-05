# SPDX-License-Identifier: GPL-3.0-or-later
"""Stone maps: which laid stone every pixel of a path's periodic block belongs to.

A path whose recipe sets `stone_edges` keeps or drops each stone as a unit at
its borders, so an edge runs along the joints instead of cutting a stone. The
runtime decides each stone once, at its centre; the stone map tells it, for
any pixel, which stone that is and how close the joint is.

The map covers the recipe's whole periodic block (grid x grid tiles of
N = 128 render pixels, the HD resolution; native pixels sample texel 4x+2) and
is the same for every layout, since the stone layout is shared by all of them.
The image is two blocks side by side. Each RGBA8 texel of the left block holds:

- R: the pixel's stone, as its index among the stones centred in one tile;
- G: high nibble, that tile's offset from the pixel's tile, (dx+1) + 3(dy+1);
  low nibble, the same for the nearest other stone;
- B: the index of the nearest other stone (R itself, and the low nibble of G
  the high one, beyond the joint band);
- A: distance to the joint with it, in native pixels x 32, clamped.

The right block names the second-nearest other stone the same way (R: index,
G: low nibble tile offset, A: joint distance), so a pixel where three stones
meet knows all of them, and in B holds the pixel's signed distance from its
own stone's face (negative in the joint), 128 + native pixels x 32, clamped:
a kept stone beside a dropped one is drawn to its face, not into the joint.

Stones are numbered per tile so that a 64-bit mask per tile holds their
decisions; a tile may hold the centres of at most MAX_STONES_PER_TILE stones.
"""
import numpy as np
from PIL import Image
from scipy import ndimage

from terrain_core import N, NATIVE_PX, RECIPES, Ctx

MAX_STONES_PER_TILE = 64
# The joint band: how far from a joint a pixel still records the stone across
# it, in render pixels. The runtime's rim and contact shadow end well inside.
JOINT_REACH = 4 * NATIVE_PX
DISTANCE_SCALE = 32  # A channel units per native pixel
# Painted stones round their corners (bevels and chipping), so a stone's
# visible face is its geometric face opened by a disc of this radius, in
# render pixels: sharp tips, which the paint fills with joint, fall outside.
FACE_ROUNDING = 1.5 * NATIVE_PX


def rounded_faces(faces):
    """Signed distance from each pixel to the visible stone face, in render
    pixels, positive inside: the geometric faces opened by FACE_ROUNDING,
    measured on the periodic block."""
    size = faces.shape[0]
    tiled = np.tile(faces > 0, (3, 3))
    r = int(np.ceil(FACE_ROUNDING))
    yy, xx = np.mgrid[-r:r + 1, -r:r + 1]
    disc = xx * xx + yy * yy <= FACE_ROUNDING * FACE_ROUNDING
    opened = ndimage.binary_opening(tiled, structure=disc)
    inside = ndimage.distance_transform_edt(opened)
    outside = ndimage.distance_transform_edt(~opened)
    signed = np.where(opened, inside - 0.5, 0.5 - outside)
    return signed[size:2 * size, size:2 * size]


def fold_inlays(ids, faces, inlays):
    """Give each inlay pixel to the nearest ordinary stone, as joint: an inlay
    then shows wherever the stones around it are kept and goes with them."""
    if not inlays:
        return ids, faces
    size = ids.shape[0]
    inlay = np.isin(ids, list(inlays))
    tiled = np.tile(inlay, (3, 3))
    _, (iy, ix) = ndimage.distance_transform_edt(tiled, return_indices=True)
    nearest = np.tile(ids, (3, 3))[iy, ix][size:2 * size, size:2 * size]
    return np.where(inlay, nearest, ids), np.where(inlay, -NATIVE_PX, faces)


def stone_geometry(name):
    """Render layout 0 and return its recorded (ids, faces, centres, size),
    with inlays folded into the stones around them."""
    rec = RECIPES[name]
    if not rec.stone_edges:
        raise ValueError(f"{name} does not lay stones with whole-stone edges")
    size = rec.grid * N
    ctx = Ctx(name, 0, 0, rec.phases, size=size)
    rec.render(ctx)
    if ctx.stones is None:
        raise RuntimeError(f"{name} did not record its stones")
    ids, edge, centres, inlays = ctx.stones
    ids, faces = fold_inlays(np.asarray(ids, dtype=np.int64).reshape(size, size),
                             np.asarray(edge, dtype=np.float64).reshape(size, size), inlays)
    return ids, faces, np.asarray(centres, dtype=np.float64), size


def encode(ids, faces, centres, size, tile=N):
    """The RGBA8 stone map of one periodic block."""
    tiles = size // tile
    count = len(centres)
    # Number each stone among those centred in the same tile.
    home = (np.floor(centres[:, 1] / tile).astype(int) % tiles) * tiles + \
           (np.floor(centres[:, 0] / tile).astype(int) % tiles)
    local = np.zeros(count, dtype=np.int64)
    seen = {}
    for k in range(count):
        local[k] = seen.get(home[k], 0)
        seen[home[k]] = local[k] + 1
    if max(seen.values()) > MAX_STONES_PER_TILE:
        raise ValueError(f"a tile holds the centres of {max(seen.values())} stones; "
                         f"at most {MAX_STONES_PER_TILE} fit its decision mask")
    ys, xs = np.mgrid[0:size, 0:size]
    pixel_tile_x, pixel_tile_y = xs // tile, ys // tile

    def offset_code(stone):
        hx, hy = home[stone] % tiles, home[stone] // tiles
        dx = (hx - pixel_tile_x + tiles // 2) % tiles - tiles // 2
        dy = (hy - pixel_tile_y + tiles // 2) % tiles - tiles // 2
        if np.abs(dx).max() > 1 or np.abs(dy).max() > 1:
            raise ValueError("a stone reaches more than one tile from the tile holding its centre")
        return (dx + 1) + 3 * (dy + 1)

    # The nearest pixels of two other stones, searching outward ring by ring
    # over the periodic block, give the stones across the nearest joints.
    across = ids.copy()
    distance = np.full(ids.shape, np.inf)
    second = ids.copy()
    second_distance = np.full(ids.shape, np.inf)
    reach = int(np.ceil(JOINT_REACH))
    offsets = sorted(((dx * dx + dy * dy) ** 0.5, dx, dy)
                     for dy in range(-reach, reach + 1) for dx in range(-reach, reach + 1)
                     if 0 < dx * dx + dy * dy <= JOINT_REACH * JOINT_REACH)
    for d, dx, dy in offsets:
        other = np.roll(ids, (-dy, -dx), axis=(0, 1))
        found = (other != ids) & np.isinf(distance)
        across[found] = other[found]
        distance[found] = d
        later = (other != ids) & (other != across) & ~found & ~np.isinf(distance) & np.isinf(second_distance)
        second[later] = other[later]
        second_distance[later] = d
    def joint_alpha(found):
        # The joint lies halfway to the first pixel of the other stone.
        joint = np.where(np.isinf(found), np.inf, found - 0.5)
        return np.clip(np.round(joint / NATIVE_PX * DISTANCE_SCALE), 0, 255).astype(np.uint8)

    rgba = np.zeros((size, 2 * size, 4), dtype=np.uint8)
    rgba[:, :size, 0] = local[ids]
    rgba[:, :size, 1] = (offset_code(ids) << 4) | offset_code(across)
    rgba[:, :size, 2] = local[across]
    rgba[:, :size, 3] = joint_alpha(distance)
    rgba[:, size:, 0] = local[second]
    rgba[:, size:, 1] = offset_code(second)
    visible = rounded_faces(faces)
    rgba[:, size:, 2] = np.clip(np.round(128 + visible / NATIVE_PX * DISTANCE_SCALE), 0, 255).astype(np.uint8)
    rgba[:, size:, 3] = joint_alpha(second_distance)
    return rgba


def stone_map(name):
    ids, faces, centres, size = stone_geometry(name)
    return Image.fromarray(encode(ids, faces, centres, size), "RGBA")


def decode(rgba, tiles):
    """Inverse of encode's left block for tests: per pixel (own, across) as
    (tile x, tile y, index) in block tiles, and the joint distance in native pixels."""
    size = rgba.shape[0]
    rgba = rgba[:, :size]
    tile = size // tiles
    ys, xs = np.mgrid[0:size, 0:size]
    base_x, base_y = xs // tile, ys // tile

    def stone(code, index):
        return ((base_x + code % 3 - 1) % tiles, (base_y + code // 3 - 1) % tiles, index)

    own = stone(rgba[..., 1] >> 4, rgba[..., 0].astype(int))
    across = stone(rgba[..., 1] & 15, rgba[..., 2].astype(int))
    return own, across, rgba[..., 3] / DISTANCE_SCALE
