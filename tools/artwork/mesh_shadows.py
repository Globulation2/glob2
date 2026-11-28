#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cast building ground shadows from TRELLIS meshes, under the canonical sun.

Offline: no network. For each building frame, from the meshes that
`tools/artwork/ai/trellis_meshes.py` staged (one per seed) and the HD sprite,

1. **align** each mesh to the sprite with an orthographic camera at the one
   shared 3/4-view elevation `ELEVATION_DEG`: the mesh's up axis is the GLB +y;
   yaw is searched over the full turn, mirrored and not, then yaw, roll, scale
   and translation are refined (Powell) on a soft silhouette IoU against the
   sprite's silhouette (base alpha >= 200 or team alpha >= 20, as
   cast_shadows.py casts from);
2. **gate** it: tolerant silhouette fit >= `MIN_FIT`, and a solid model rather than a
   relief: the mesh's narrower horizontal extent is at least `MIN_DEPTH` of its
   wider one, and its tallest point stands no higher on screen than the sprite
   is tall. The best-fitting seed that passes is kept; a frame none pass keeps
   its block spec in `content-src/gfx/shadows/specs.json`;
3. **cast** it: every visible surface point H screen px above the mesh's ground
   plane (a world height h shows as h * cos(elevation) on screen) at sprite
   pixel (x, y) stands on the ground at (x, y + H) and throws its shadow
   `length` * H (at most `cast_shadows` CAP, 180 HD px) along the sun
   (`Sunlight.h`), blurred 2 px. The length is the frame's `length` in
   specs.json (default `cast_shadows.SHADOW_LENGTH`), art direction per building:
   low buildings short, towers long. Sprite pixels the mesh misses cast at the
   nearest visible height.

It writes, per frame, `<frame>s.png` (HD, black, alpha = coverage, the sprite's
top-left corner, extended right and bottom as far as the shadow reaches) and
`<frame>.json` (mesh, seed, input and sprite hashes, camera, gate metrics,
shadow length) to `--out` (`content-src/gfx/shadows/mesh/` for the committed
source data). `cast_shadows.py`'s `mesh` method pads and registers those masks
offline. A review sheet per frame shows the fit and the shadow.

    python tools/artwork/mesh_shadows.py --meshes /tmp/glob2-meshes --sprites content/data/highres/v1 \\
        --out content-src/gfx/shadows/mesh [--frames a b] [--review DIR]
    python tools/artwork/mesh_shadows.py --meshes DIR --sprites DIR --fit-elevation   # refit the shared elevation

Review sheets show the fit, the height map, the shadow at the frame's length and
at the neighbouring candidate lengths (`REVIEW_LENGTHS`), the classic result and,
with `--compare-specs`, the block spec it replaces.

Needs the environment in tools/artwork/ai/requirements-mesh.txt (numpy, scipy,
Pillow, trimesh).
"""
import argparse
import hashlib
import json
import math
import multiprocessing
import os
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage, optimize

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import cast_shadows  # noqa: E402

# The sprites' shared 3/4 view: the camera looks down this far below the
# horizontal. Refit from every gated building with --fit-elevation.
ELEVATION_DEG = 59.6
SHADOW_LENGTH = cast_shadows.SHADOW_LENGTH  # screen px of throw per screen px of height
CAP = cast_shadows.DEFAULTS["cap"]
BLUR = cast_shadows.DEFAULTS["blur"]
REVIEW_LENGTHS = (0.2, 0.3, 0.45, 0.6, 0.8, 1.1)
MIN_FIT = 0.80    # tolerant silhouette F-score (tolerant_fit)
TOLERANCE = 0.015  # of the sprite's larger side
MIN_DEPTH = 0.5
POINTS = 400_000
MARGIN = 256
OUTPUT = Path("content-src/gfx/shadows/mesh")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_rgba(path):
    with Image.open(path) as image:
        return np.asarray(image.convert("RGBA"))


def silhouette(base, team):
    """What casts: base alpha >= 200 or team alpha >= 20 (cast_shadows' block silhouette)."""
    body = base[..., 3] >= cast_shadows.DEFAULTS["body_alpha"]
    if team is not None:
        h, w = min(team.shape[0], body.shape[0]), min(team.shape[1], body.shape[1])
        body[:h, :w] |= team[:h, :w, 3] >= cast_shadows.DEFAULTS["team_alpha"]
    return body


def tolerant_fit(mesh, body, tolerance):
    """F-score of the two silhouettes, each pixel counting as matched within
    `tolerance` px of the other: thin rails and masts a few px off still match."""
    if not mesh.any() or not body.any():
        return 0.0
    near_body = ndimage.distance_transform_edt(~body) <= tolerance
    near_mesh = ndimage.distance_transform_edt(~mesh) <= tolerance
    precision = (mesh & near_body).sum() / mesh.sum()
    recall = (body & near_mesh).sum() / body.sum()
    return float(2 * precision * recall / max(precision + recall, 1e-9))


def iou(a, b):
    union = (a | b).sum()
    return float((a & b).sum() / union) if union else 0.0


# --- mesh and camera ---------------------------------------------------------------

def load_points(path, count=POINTS, seed=0):
    """Surface points of the mesh, centred and scaled to unit size, z up (GLB is y up)."""
    import trimesh
    mesh = trimesh.load(path, force="mesh", process=False)
    vertices = np.asarray(mesh.vertices, float)
    low, high = vertices.min(0), vertices.max(0)
    mesh.vertices = (vertices - (low + high) / 2) / (high - low).max()
    points, _ = trimesh.sample.sample_surface(mesh, count, seed=seed)
    points = np.asarray(points)
    return np.stack([points[:, 0], -points[:, 2], points[:, 1]], 1)  # y up -> z up, right-handed


def camera(yaw, elevation, roll):
    """World (z up) to camera rows: screen right, screen up, toward the viewer."""
    cy, sy = math.cos(yaw), math.sin(yaw)
    turn = np.array([[cy, -sy, 0], [sy, cy, 0], [0, 0, 1]])
    ce, se = math.cos(elevation), math.sin(elevation)
    tilt = np.array([[1, 0, 0], [0, se, ce], [0, -ce, se]])
    cr, sr = math.cos(roll), math.sin(roll)
    spin = np.array([[cr, -sr, 0], [sr, cr, 0], [0, 0, 1]])
    return spin @ tilt @ turn


def project(points, params, elevation):
    """Image coordinates (x right, y down) and depth toward the viewer."""
    yaw, roll, log_scale, tx, ty = params
    view = points @ camera(yaw, elevation, roll).T
    scale = math.exp(log_scale)
    return np.stack([view[:, 0] * scale + tx, -view[:, 1] * scale + ty], 1), view[:, 2]


def splat(xy, shape):
    h, w = shape
    ix, iy = np.rint(xy[:, 0]).astype(int), np.rint(xy[:, 1]).astype(int)
    ok = (ix >= 0) & (ix < w) & (iy >= 0) & (iy < h)
    mask = np.zeros(shape, bool)
    mask[iy[ok], ix[ok]] = True
    # No hole filling: rings (inn, pool, racetrack) have open middles the sprite shows.
    return ndimage.binary_closing(mask, iterations=2, border_value=0) | mask


def resize_mask(mask, factor):
    h, w = mask.shape
    image = Image.fromarray(mask.astype(np.uint8) * 255)
    return np.asarray(image.resize((max(1, round(w * factor)), max(1, round(h * factor))),
                                   Image.Resampling.BILINEAR)) > 127


def box_fit(xy, target):
    """Scale and translation putting the projected box on the target's box."""
    ys, xs = np.nonzero(target)
    tw, th = np.ptp(xs) + 1, np.ptp(ys) + 1
    pw, ph = np.ptp(xy[:, 0]) + 1e-6, np.ptp(xy[:, 1]) + 1e-6
    scale = math.sqrt(tw * th / (pw * ph))
    return (math.log(scale), (xs.max() + xs.min()) / 2 - (xy[:, 0].max() + xy[:, 0].min()) / 2 * scale,
            (ys.max() + ys.min()) / 2 - (xy[:, 1].max() + xy[:, 1].min()) / 2 * scale)


def align(points, target, elevation):
    """(params (yaw, roll, log scale, tx, ty) in full-size pixels, mirrored, IoU)."""
    rng = np.random.default_rng(1)
    h, w = target.shape
    coarse_k = 96 / max(h, w)
    coarse = resize_mask(target, coarse_k)
    few = points[rng.choice(len(points), min(len(points), 12000), replace=False)]
    candidates = []
    for mirrored in (False, True):
        sample = few * (-1, 1, 1) if mirrored else few
        for yaw in np.radians(np.arange(0, 360, 10)):
            xy, _ = project(sample, (yaw, 0, 0, 0, 0), elevation)
            params = (yaw, 0.0) + box_fit(xy, coarse)
            xy, _ = project(sample, params, elevation)
            candidates.append((iou(splat(xy, coarse.shape), coarse), mirrored, params))
    candidates.sort(key=lambda c: -c[0])
    fine_k = min(1.0, 192 / max(h, w))
    fine = resize_mask(target, fine_k)
    soft = ndimage.gaussian_filter(fine.astype(float), 1.0)
    more = points[rng.choice(len(points), min(len(points), 40000), replace=False)]
    best = None
    for _, mirrored, params in candidates[:6]:
        sample = more * (-1, 1, 1) if mirrored else more
        start = np.array(params, float)
        start[2] += math.log(fine_k / coarse_k)
        start[3:] *= fine_k / coarse_k

        def loss(q):
            xy, _ = project(sample, q, elevation)
            m = ndimage.gaussian_filter(splat(xy, fine.shape).astype(float), 1.0)
            inter = (m * soft).sum()
            return -inter / (m.sum() + soft.sum() - inter + 1e-9)

        q = optimize.minimize(loss, start, method="Powell",
                              options={"xtol": 1e-3, "ftol": 1e-4, "maxfev": 1500}).x
        xy, _ = project(sample, q, elevation)
        score = iou(splat(xy, fine.shape), fine)
        if best is None or score > best[0]:
            best = (score, mirrored, q)
    _, mirrored, q = best
    q = q.copy()
    # Last, the same refinement at up to 384 px, where a pixel is at most 2 HD px.
    final_k = min(1.0, 384 / max(h, w))
    q[2] += math.log(final_k / fine_k)
    q[3:] *= final_k / fine_k
    final = resize_mask(target, final_k)
    soft = ndimage.gaussian_filter(final.astype(float), 1.0)
    sample = more * (-1, 1, 1) if mirrored else more

    def loss(q):
        xy, _ = project(sample, q, elevation)
        m = ndimage.gaussian_filter(splat(xy, final.shape).astype(float), 1.0)
        inter = (m * soft).sum()
        return -inter / (m.sum() + soft.sum() - inter + 1e-9)

    q = optimize.minimize(loss, q, method="Powell", options={"xtol": 1e-3, "ftol": 1e-5, "maxfev": 1500}).x
    q[2] -= math.log(final_k)
    q[3:] /= final_k
    sample = (points * (-1, 1, 1) if mirrored else points)[::4]
    xy, _ = project(sample, q, elevation)
    return q, mirrored, iou(splat(xy, target.shape), target)


def free_elevation(points, target):
    """Best elevation for one frame (for --fit-elevation): coarse search, then refine."""
    best = max(((align(points[::8], target, math.radians(e))[2], e) for e in range(40, 81, 5)))
    refined = optimize.minimize_scalar(lambda e: -align(points[::8], target, math.radians(e))[2],
                                       bounds=(best[1] - 5, best[1] + 5), method="bounded",
                                       options={"xatol": 0.5})
    return float(refined.x)


# --- heights and casting ------------------------------------------------------------

def heights(points, params, elevation, ground_percentile=0.5):
    """Screen px height of each point above the mesh's ground plane."""
    up = points[:, 2]
    return np.clip(up - np.percentile(up, ground_percentile), 0, None) * math.exp(params[2]) * math.cos(elevation)


def visible_heights(xy, depth, values, body):
    """Height of the nearest surface per sprite pixel, filled from the nearest hit over the body."""
    h, w = body.shape
    ix, iy = np.rint(xy[:, 0]).astype(int), np.rint(xy[:, 1]).astype(int)
    ok = (ix >= 0) & (ix < w) & (iy >= 0) & (iy < h)
    order = np.argsort(depth[ok])  # far first: nearer points overwrite
    grid = np.full((h, w), np.nan)
    grid[iy[ok][order], ix[ok][order]] = values[ok][order]
    hit = np.isfinite(grid)
    if not hit.any():
        return np.where(body, 0.0, np.nan)
    nearest = ndimage.distance_transform_edt(~hit, return_distances=False, return_indices=True)
    return np.where(body, grid[tuple(nearest)], np.nan)


def sweep(mask, vector):
    steps = int(max(abs(vector[0]), abs(vector[1]))) + 1
    out = np.zeros_like(mask)
    h, w = mask.shape
    for t in np.linspace(0, 1, steps + 1):
        dx, dy = int(round(t * vector[0])), int(round(t * vector[1]))
        out[dy:, dx:] |= mask[:h - dy, :w - dx]
    return out


def cast(xs, ys, hs, shape, length=SHADOW_LENGTH, cap=CAP, bin_width=3.0):
    """Shadow of points (screen x, y, height H): from (x, y + H), min(length * H, cap) along the sun."""
    out = np.zeros(shape, bool)
    h, w = shape
    keep = hs > 0.5
    xs, ys, hs = xs[keep], ys[keep], hs[keep]
    bins = np.floor(hs / bin_width).astype(int)
    for b in np.unique(bins):
        sel = bins == b
        ground = np.zeros(shape, bool)
        gx, gy = np.rint(xs[sel]).astype(int), np.rint(ys[sel] + hs[sel]).astype(int)
        ok = (gx >= 0) & (gx < w) & (gy >= 0) & (gy < h)
        ground[gy[ok], gx[ok]] = True
        throw = min((b + 0.5) * bin_width * length, cap)
        out |= sweep(ground, np.array(cast_shadows.SHADOW) * throw)
    return ndimage.binary_closing(out) | out


GROUND_MARGIN = 4  # HD px a ground point may land below its piece's lowest pixel
PIECE_JOIN = 3     # HD px: sprite parts this close are one piece


def ground_clamp(xs, ys, hs, body):
    """Heights limited so every ground point (x, y + H) stays on its own piece of the
    sprite: no lower than that piece's lowest pixel in the column (a 3/4 view shows
    a building's base at the bottom of its silhouette). This removes slabs below
    the base from a mesh seated too low, and keeps separate pieces (construction
    sites, scattered parts) from casting across each other. Returns the heights
    and each point's piece."""
    pieces, count = ndimage.label(ndimage.binary_dilation(body, iterations=PIECE_JOIN) & ~np.zeros_like(body))
    pieces = np.where(ndimage.binary_dilation(body, iterations=PIECE_JOIN), pieces, 0)
    h, w = body.shape
    rows = np.arange(h)[:, None]
    nearest = ndimage.distance_transform_edt(~body, return_distances=False, return_indices=True)
    owner = pieces[nearest[0], nearest[1]]  # every pixel's nearest piece
    bottom = np.full((count + 1, w), -1)
    for label in range(1, count + 1):
        mask = (pieces == label) & body
        bottom[label] = np.where(mask.any(0), (rows * mask).max(0), -1)
        known = bottom[label] >= 0
        if known.any():  # columns beside the piece take its nearest column's base
            index = ndimage.distance_transform_edt(~known, return_distances=False, return_indices=True)[0]
            bottom[label] = bottom[label][index]
    ix = np.clip(np.rint(xs).astype(int), 0, w - 1)
    iy = np.clip(np.rint(ys).astype(int), 0, h - 1)
    piece = owner[iy, ix]
    limit = bottom[piece, ix] + GROUND_MARGIN - ys
    return np.clip(np.minimum(hs, limit), 0, None), piece


def attached(shadow, body, reach=4):
    """Only shadow parts that touch the sprite's silhouette: no phantom blobs."""
    labels, _ = ndimage.label(shadow)
    keep = np.unique(labels[ndimage.binary_dilation(body, iterations=reach) & shadow])
    return np.isin(labels, keep[keep > 0])


def mask_image(shadow, h0, w0):
    """Blurred coverage, cropped to the frame's top-left corner and the shadow's reach (uint8)."""
    soft = ndimage.gaussian_filter(shadow.astype(float), BLUR)
    ys, xs = np.nonzero(soft > 0.5 / 255)
    height = max(h0, ys.max() + 1 if len(ys) else 0)
    width = max(w0, xs.max() + 1 if len(xs) else 0)
    return np.rint(np.clip(soft[:height, :width], 0, 1) * 255).astype(np.uint8)


# --- frames ----------------------------------------------------------------------------

def evaluate(job):
    """Align, gate and cast one (frame, seed) mesh."""
    frame, seed, mesh_path, base_path, team_path, elevation_deg, length, review = job[:8]
    spec = job[8] if len(job) > 8 else {"strip": True}
    base = cast_shadows.deshadow(load_rgba(base_path), None, spec)[0]
    team = load_rgba(team_path) if team_path else None
    body = silhouette(base, team)
    elevation = math.radians(elevation_deg)
    points = load_points(mesh_path)
    params, mirrored, fit = align(points, body, elevation)
    if mirrored:
        points = points * (-1, 1, 1)
    xy, depth = project(points, params, elevation)
    hs = heights(points, params, elevation)
    # A relief stands up facing the camera: seen from above along the view, its
    # ground depth is small against its width. A real footprint is about square.
    # Measured on the foundation (the lowest fifth of the mesh), so banners and
    # blades held out sideways do not count.
    yaw = params[0]
    up = points[:, 2]
    base = points[up <= up.min() + 0.2 * np.ptp(up)]
    across = base[:, 0] * math.cos(yaw) - base[:, 1] * math.sin(yaw)
    along = base[:, 0] * math.sin(yaw) + base[:, 1] * math.cos(yaw)
    depth_ratio = float(np.ptp(along) / max(np.ptp(across), 1e-9))
    h0, w0 = body.shape
    grid = visible_heights(xy, depth, hs, body)
    near = ndimage.binary_dilation(body, iterations=2)
    ix, iy = np.rint(xy[:, 0]).astype(int), np.rint(xy[:, 1]).astype(int)
    inside = (ix >= 0) & (ix < w0) & (iy >= 0) & (iy < h0)
    inside[inside] = near[iy[inside], ix[inside]]
    by, bx = np.nonzero(body)
    cx, cy = np.concatenate([xy[inside, 0], bx]), np.concatenate([xy[inside, 1], by])
    raw = np.concatenate([hs[inside], grid[by, bx]])
    clamped, _ = ground_clamp(cx, cy, raw, body)
    casters = (cx, cy, clamped)
    ys = np.nonzero(body.any(1))[0]
    sprite_height = int(ys.max() - ys.min() + 1) if len(ys) else 0
    clamped_grid = np.where(body, np.nan, np.nan)
    clamped_grid[by, bx] = clamped[-len(bx):] if len(bx) else []
    tallest = float(np.nanmax(clamped_grid)) if np.isfinite(clamped_grid).any() else 0.0
    unclamped = float(np.nanmax(grid)) if np.isfinite(grid).any() else 0.0
    xy_body = splat(xy, (h0, w0))
    tolerant = tolerant_fit(xy_body, body, max(2.0, TOLERANCE * max(h0, w0)))
    reasons = []
    if tolerant < MIN_FIT:
        reasons.append(f"silhouette fit {tolerant:.3f} < {MIN_FIT}")
    if depth_ratio < MIN_DEPTH:
        reasons.append(f"relief: depth ratio {depth_ratio:.2f} < {MIN_DEPTH}")
    if tallest > sprite_height:
        reasons.append(f"taller ({tallest:.0f} px) than the sprite ({sprite_height} px)")
    shape = (h0 + MARGIN, w0 + MARGIN)
    padded = np.zeros(shape, bool)
    padded[:h0, :w0] = body
    masks = {value: mask_image(attached(cast(*casters, shape, value), padded), h0, w0)
             for value in set(review) | {length}}
    return {
        "frame": frame, "seed": seed, "mesh": str(mesh_path), "mirrored": bool(mirrored),
        "camera": {"elevation_deg": elevation_deg, "yaw_deg": round(math.degrees(params[0]) % 360, 2),
                   "roll_deg": round(math.degrees(params[1]), 2), "scale_px": round(math.exp(params[2]), 3),
                   "tx": round(float(params[3]), 2), "ty": round(float(params[4]), 2), "mirrored": bool(mirrored),
                   "up": "glb +y"},
        "metrics": {"silhouette_fit": round(tolerant, 4), "silhouette_iou": round(fit, 4),
                    "depth_ratio": round(depth_ratio, 3), "max_height_unclamped_hd": round(unclamped, 1),
                    "max_height_hd": round(tallest, 1),
                    "p95_height_hd": round(float(np.nanpercentile(clamped_grid[body], 95)), 1) if body.any() else 0.0,
                    "sprite_height_hd": sprite_height},
        "passed": not reasons, "reasons": reasons, "length": length,
        "mask": masks[length], "masks": masks, "fit_overlay": (body, xy_body), "heights": clamped_grid,
    }


def candidates(meshes, frame):
    directory = Path(meshes) / frame
    return sorted(directory.glob("seed*.glb"), key=lambda p: int(p.stem[4:])) if directory.is_dir() else []


SIBLING_RATIO = 2.5  # a mesh this much taller or flatter than its variants' is an outlier


def family(frame):
    """Variants of one building: hosp0b0 and hosp0b1, not the construction site hosp0c0."""
    return frame.rstrip("0123456789")


def choose(results, sibling_height=None):
    """Among passing seeds within 0.05 of the best fit: the one whose height is
    closest to the building's other variants (`sibling_height`, their median p95),
    else the best fit. None when no seed passes."""
    passing = [r for r in results if r["passed"]]
    if not passing:
        return None
    best = max(r["metrics"]["silhouette_fit"] for r in passing)
    near = [r for r in passing if r["metrics"]["silhouette_fit"] >= best - 0.05]
    if sibling_height:
        return min(near, key=lambda r: abs(math.log(max(r["metrics"]["p95_height_hd"], 1.0) / sibling_height)))
    return max(near, key=lambda r: r["metrics"]["silhouette_fit"])


def best_failed(results, sibling_height=None):
    """For a frame the art direction keeps on its mesh although no seed passes: the
    best fit among seeds that are not reliefs (else among all), nearest its variants' height."""
    solid = [r for r in results if not any("relief" in reason for reason in r["reasons"])] or list(results)
    if not solid:
        return None
    best = max(r["metrics"]["silhouette_fit"] for r in solid)
    near = [r for r in solid if r["metrics"]["silhouette_fit"] >= best - 0.05]
    if sibling_height:
        return min(near, key=lambda r: abs(math.log(max(r["metrics"]["p95_height_hd"], 1.0) / sibling_height)))
    return max(near, key=lambda r: r["metrics"]["silhouette_fit"])


def outlier(result, sibling_height):
    p95 = max(result["metrics"]["p95_height_hd"], 1.0)
    return bool(sibling_height) and not (1 / SIBLING_RATIO <= p95 / sibling_height <= SIBLING_RATIO)


def record(result, meshes, base_path, team_path, length):
    """The committed JSON beside a mask."""
    staged = json.loads(Path(result["mesh"]).with_suffix(".json").read_text())
    versions = {}
    for module in ("numpy", "scipy", "PIL", "trimesh"):
        versions[module] = __import__(module).__version__
    return {
        "frame": result["frame"], "model": staged["model"], "endpoint": staged["endpoint"],
        "seed": result["seed"], "request_id": staged.get("request_id"),
        "mesh_sha256": staged["mesh_sha256"], "input_sha256": staged["input_sha256"],
        "input_sources": staged["sources"],
        "sprite": {"base_sha256": sha(base_path), "team_sha256": sha(team_path) if team_path else None},
        "camera": result["camera"], "metrics": result["metrics"],
        "gate": {"passed": result["passed"], "reasons": result["reasons"]},
        "shadow": {"length": length, "cap_hd": CAP, "blur_hd": BLUR, "sun": list(cast_shadows.SHADOW)},
        "tool_sha256": sha(Path(__file__).resolve()), "packages": versions,
    }


def neighbours(length):
    """The frame's length and the candidate lengths either side of it."""
    values = sorted(set(REVIEW_LENGTHS) | {length})
    i = values.index(length)
    return values[max(0, i - 1):i + 2]


def review_sheet(result, base, team, path, alternative=None, classic=None):
    """Fit overlay, height map, the shadow at its length and its neighbours, the
    classic result and the block spec it replaces, at the game's opacity."""
    body, mesh = result["fit_overlay"]
    overlay = np.zeros(body.shape + (3,), np.uint8) + 30
    overlay[body] = (40, 190, 90)
    overlay[mesh] = (220, 60, 200)
    overlay[body & mesh] = (235, 235, 235)
    grid = np.nan_to_num(result["heights"])
    t = np.clip(grid / max(60.0, grid.max()), 0, 1)
    heat = np.dstack([255 * np.clip(1.5 * t, 0, 1), 255 * np.clip(1.5 * t - 0.5, 0, 1), 255 * np.clip(0.6 - t, 0, 1)])
    heat[~body] = (30, 30, 30)
    panels = [(Image.fromarray(overlay), f"fit {result['metrics']['silhouette_fit']:.3f} (IoU {result['metrics']['silhouette_iou']:.3f})"),
              (Image.fromarray(heat.astype(np.uint8)), f"height, max {result['metrics']['max_height_hd']:.0f} px")]
    for value in neighbours(result["length"]):
        mark = " (assigned)" if value == result["length"] else ""
        panels.append((cast_shadows.composite(base, team, result["masks"][value], 1.0), f"length {value}{mark}"))
    if classic is not None:
        classic_base, classic_team = classic
        hd = cast_shadows.mesh_pad(result["mask"], classic_base.shape[0] * 4, classic_base.shape[1] * 4)
        image = cast_shadows.composite(classic_base, classic_team, cast_shadows.downscale(hd), 1.0)
        panels.append((image.resize((image.width * 4, image.height * 4), Image.Resampling.NEAREST),
                       f"classic, length {result['length']}"))
    if alternative is not None:
        panels.append((cast_shadows.composite(base, team, alternative, 1.0), "old block spec"))
    height = max(p.height for p, _ in panels) + 18
    sheet = Image.new("RGB", (sum(p.width for p, _ in panels) + 6 * len(panels), height), (24, 24, 28))
    draw = ImageDraw.Draw(sheet)
    x = 0
    for image, text in panels:
        sheet.paste(image.convert("RGB"), (x, 18))
        draw.text((x + 3, 3), text if x else f"{result['frame']} seed {result['seed']}: {text}",
                  fill=(235, 235, 235))
        x += image.width + 6
    sheet.save(path)


def footprints(root=ROOT):
    """{sprite prefix: (tiles wide, tiles high, level from 1, building type)} from the building catalog."""
    out = {}
    for path in sorted((root / "content/data/buildings").glob("*.json")):
        for variant in json.loads(path.read_text()).get("variants", []):
            props = variant.get("properties", {})
            sprite = props.get("gameSprite", "").rsplit("/", 1)[-1]
            if sprite and sprite not in out:
                level = int(variant["key"].split(".")[1]) + 1 if variant["key"].count(".") >= 2 else 1
                out[sprite] = (props.get("width", 1), props.get("height", 1), level, props.get("type", path.stem))
    return out


def length_strip(result, base, team, path):
    """The frame at every review length, at game size (HD at zoom 1) above 2x, the assigned one marked."""
    columns = []
    for value in REVIEW_LENGTHS:
        mask = result["masks"][value]
        zoom1 = cast_shadows.composite(base, team, mask, 0.25)
        zoom2 = cast_shadows.composite(base, team, mask, 0.5)
        assigned = value == result["length"]
        column = Image.new("RGB", (zoom2.width, 22 + zoom1.height + 6 + zoom2.height),
                           (60, 52, 20) if assigned else (24, 24, 28))
        draw = ImageDraw.Draw(column)
        draw.text((4, 4), f"{value}" + ("  ASSIGNED" if assigned else ""), fill=(255, 230, 120) if assigned else (235, 235, 235))
        column.paste(zoom1, (0, 22))
        column.paste(zoom2, (0, 22 + zoom1.height + 6))
        columns.append(column)
    strip = Image.new("RGB", (sum(c.width for c in columns) + 6 * (len(columns) - 1), max(c.height for c in columns) + 20),
                      (24, 24, 28))
    ImageDraw.Draw(strip).text((4, 3), f"{result['frame']}: shadow length (px of throw per px of height), "
                               f"game size above 2x, opacity 0.32", fill=(235, 235, 235))
    x = 0
    for column in columns:
        strip.paste(column, (x, 20))
        x += column.width + 6
    strip.save(path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--meshes", type=Path, required=True, help="trellis_meshes.py staging directory")
    parser.add_argument("--sprites", type=Path, nargs="+", default=[ROOT / cast_shadows.PACK],
                        help="HD sprite directories, searched in order")
    parser.add_argument("--frames", nargs="*", help="default: every staged frame")
    parser.add_argument("--out", type=Path, help=f"mask and record directory (committed: {OUTPUT})")
    parser.add_argument("--review", type=Path, help="write a review sheet per frame here")
    parser.add_argument("--fit-elevation", action="store_true", help="refit the shared elevation and exit")
    parser.add_argument("--specs", type=Path, help="shadow specs (default: the committed specs.json) for each "
                        "frame's length")
    parser.add_argument("--compare-specs", type=Path, help="block specs to show beside the mesh shadow")
    parser.add_argument("--classic", type=Path, help="classic sprites, for a classic panel on review sheets")
    parser.add_argument("--lengths", type=Path, help="write a length strip per frame and index.json here")
    parser.add_argument("--accept-failed", nargs="*", help="frames kept on their best mesh although no seed passes "
                        "the gate (recorded as failed)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count())
    args = parser.parse_args(argv)
    frames = args.frames or sorted(p.name for p in args.meshes.iterdir() if p.is_dir() and "." not in p.name)
    specs = cast_shadows.load_specs(path=args.specs) if (args.specs or (ROOT / cast_shadows.SPECS).exists()) else {}
    review = REVIEW_LENGTHS if (args.review or args.lengths) else ()

    def length_of(frame):
        spec = specs.get(frame)
        return spec["length"] if spec else SHADOW_LENGTH

    def sprite(name):
        return next((d / name for d in args.sprites if (d / name).is_file()), None)

    jobs = []
    for frame in frames:
        base_path, team_path = sprite(f"{frame}.png"), sprite(f"{frame}r.png")
        if base_path is None:
            raise SystemExit(f"{frame}: no HD base in {args.sprites}")
        for mesh in candidates(args.meshes, frame):
            jobs.append((frame, int(mesh.stem[4:]), mesh, base_path, team_path, ELEVATION_DEG, length_of(frame),
                         review, specs.get(frame, {"strip": True})))
    context = multiprocessing.get_context("spawn")
    if args.fit_elevation:
        with context.Pool(args.jobs) as pool:
            fits = pool.map(_free_fit, [j[:5] for j in jobs])
        values = sorted(e for e, fit in fits if fit >= MIN_FIT)
        print(f"free elevations of {len(values)} of {len(fits)} meshes fitting >= {MIN_FIT}: "
              f"{[round(v, 1) for v in values]}")
        print(f"median {np.median(values):.1f} deg; set ELEVATION_DEG")
        return
    with context.Pool(args.jobs) as pool:
        results = pool.map(evaluate, jobs, chunksize=1)
    by_frame = {}
    for result in results:
        by_frame.setdefault(result["frame"], []).append(result)
    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
    if args.review:
        args.review.mkdir(parents=True, exist_ok=True)
    summary = {}
    provisional = {f: choose(by_frame.get(f, [])) for f in frames}
    for frame in frames:
        tried = sorted(by_frame.get(frame, []), key=lambda r: r["seed"])
        heights_of_family = [r["metrics"]["p95_height_hd"] for g, r in provisional.items()
                             if r and family(g) == family(frame)]
        sibling = float(np.median(heights_of_family)) if len(heights_of_family) > 1 else None
        chosen = choose(tried, sibling)
        while chosen and outlier(chosen, sibling):
            chosen["reasons"].append(f"height {chosen['metrics']['p95_height_hd']:.0f} px against its variants' "
                                     f"{sibling:.0f} px")
            chosen["passed"] = False
            chosen = choose(tried, sibling)
        if not chosen and frame in (args.accept_failed or ()):
            chosen = best_failed(tried, sibling)
        summary[frame] = {"chosen_seed": chosen["seed"] if chosen else None, "sibling_height": sibling,
                          "seeds": {r["seed"]: dict(r["metrics"], passed=r["passed"], reasons=r["reasons"])
                                    for r in tried}}
        print(f"{frame}: " + (f"seed {chosen['seed']} fit {chosen['metrics']['silhouette_fit']:.3f}" if chosen
                              else "no seed passes the gate") + "; " +
              ", ".join(f"seed {r['seed']} fit {r['metrics']['silhouette_fit']:.2f} iou {r['metrics']['silhouette_iou']:.2f} depth {r['metrics']['depth_ratio']:.2f}"
                        f"{'' if r['passed'] else ' (' + '; '.join(r['reasons']) + ')'}" for r in tried))
        base_path, team_path = sprite(f"{frame}.png"), sprite(f"{frame}r.png")
        if args.review and tried:
            base = cast_shadows.deshadow(load_rgba(base_path), None, specs.get(frame, {"strip": True}))[0]
            team = load_rgba(team_path) if team_path else None
            alternative = classic = None
            if args.compare_specs:
                old = cast_shadows.load_specs(path=args.compare_specs).get(frame)
                if old and old["method"] == "block":
                    alternative = cast_shadows.cast(base, team, old)[0]
            if args.classic and (args.classic / f"{frame}.png").is_file():
                team_classic = args.classic / f"{frame}r.png"
                classic = (cast_shadows.deshadow(load_rgba(base_path), load_rgba(args.classic / f"{frame}.png"),
                                                 specs.get(frame, {"strip": True}))[1],
                           load_rgba(team_classic) if team_classic.is_file() else None)
            for r in tried:
                name = f"{frame}.png" if r is chosen else f"{frame}_seed{r['seed']}_{'pass' if r['passed'] else 'fail'}.png"
                review_sheet(r, base, team, args.review / name, alternative, classic)
        if args.out and not chosen:  # a frame that fell back keeps no stale mask
            for stale in (args.out / f"{frame}s.png", args.out / f"{frame}.json"):
                stale.unlink(missing_ok=True)
        if args.out and chosen:
            cast_shadows.shadow_image(chosen["mask"]).save(args.out / f"{frame}s.png", optimize=False)
            entry = record(chosen, args.meshes, base_path, team_path, chosen["length"])
            (args.out / f"{frame}.json").write_text(json.dumps(entry, indent=1) + "\n")
    if args.review:  # merged with earlier runs' frames
        gate_path = args.review / "gate.json"
        gate = json.loads(gate_path.read_text()) if gate_path.exists() else {}
        gate.update(summary)
        gate_path.write_text(json.dumps(dict(sorted(gate.items())), indent=1) + "\n")
    if args.lengths:
        args.lengths.mkdir(parents=True, exist_ok=True)
        catalog = footprints()
        index = []
        for frame in frames:
            tried = by_frame.get(frame, [])
            if not tried:
                continue
            shown = choose(tried) or max(tried, key=lambda r: r["metrics"]["silhouette_fit"])
            base_path, team_path = sprite(f"{frame}.png"), sprite(f"{frame}r.png")
            base = cast_shadows.deshadow(load_rgba(base_path), None, specs.get(frame, {"strip": True}))[0]
            team = load_rgba(team_path) if team_path else None
            path = args.lengths / f"{frame}.png"
            length_strip(shown, base, team, path)
            prefix = frame.rstrip("0123456789")
            width, height, level, kind = catalog.get(prefix, (None, None, None, None))
            index.append({"frame": frame, "family": prefix, "building": kind, "level": level,
                          "length": shown["length"], "mesh_max_height_px": shown["metrics"]["max_height_hd"],
                          "sprite_height_px": shown["metrics"]["sprite_height_hd"],
                          "footprint_tiles": [width, height], "seed": shown["seed"], "gate_passed": shown["passed"],
                          "strip": str(path)})
        index_path = args.lengths / "index.json"  # merged with earlier runs' frames
        merged = {e["frame"]: e for e in (json.loads(index_path.read_text()) if index_path.exists() else [])}
        merged.update({e["frame"]: e for e in index})
        index_path.write_text(json.dumps(sorted(merged.values(), key=lambda e: e["frame"]), indent=1) + "\n")


def _free_fit(job):
    frame, seed, mesh_path, base_path, team_path = job[:5]
    body = silhouette(cast_shadows.strip(load_rgba(base_path)), load_rgba(team_path) if team_path else None)
    points = load_points(mesh_path)
    elevation = free_elevation(points, body)
    params, mirrored, _ = align(points, body, math.radians(elevation))
    xy, _ = project(points * ((-1, 1, 1) if mirrored else 1), params, math.radians(elevation))
    return elevation, tolerant_fit(splat(xy, body.shape), body, max(2.0, TOLERANCE * max(body.shape)))


if __name__ == "__main__":
    main()
