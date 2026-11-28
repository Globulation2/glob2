#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cast the ground shadows of buildings and resources into their shadow layers.

Every frame named in `content-src/gfx/shadows/specs.json` gets a shadow layer
`<frame>s` (Sprite::shadowFrame; the game composes them in one pass, see
docs/assets/lighting.md): black, its alpha the shadow's coverage at full
strength, sharing the frame's top-left corner and reaching past it to the lower
right. The renderer applies Sunlight::groundShadowOpacity.

For each frame the tool

1. removes the shadow the old art baked in, when the spec says `strip`: only
   near-black, semi-transparent base pixels (alpha < 220, every channel < 45)
   go, so translucent art (the barracks blade) stays. Removal is idempotent;
2. takes the silhouette: base alpha >= `body_alpha` (200 for buildings) or team
   layer alpha >= 20, in HD pixels;
3. casts it along the sun (`src/render/Sunlight.h` shadowX/shadowY, 53.13
   degrees below horizontal) by its spec's method:
   * `block`: the footprint is the silhouette eroded upward by the height H and
     swept H * 1.1 along the sun. `blocks` give boxes of the silhouette their
     own heights (main masses, low ring tubes); `spires` give each silhouette
     pixel in a box the height of its row above `base_y` plus `base_h`, so
     chimneys, masts and towers cast their own long, thin shadows. Throws are
     capped at 180 HD px (45 native), under the explorers' flightHeight of 52.
     A 2 px blur softens the edges;
   * `canopy` (trees): every canopy pixel is projected to the ground along the
     sun by `ratio` times its height above the trunk base, blurred, plus a soft
     contact shadow at the trunk;
4. writes the HD shadow (padded right and bottom to whole 4-native-pixel steps)
   and the classic one, which is the HD layer area-averaged 4x, so both match.

It writes de-shadowed classic and HD base layers, the `<frame>s` layers in
`content/data/gfx` and both HD pack copies (production folder `shadows/`), the
manifest entries, `frames.txt` rows (`<frame>s W H 4 <frame>s.png -`) and
`package.json` records, with the tool, spec and input hashes as provenance.
Ripe wheat (`ressource14`, `ressource19`) is re-ripened from its de-shadowed
stage-4 frame with ripen_wheat.ripen, then de-shadowed, as validate_runtime.py
recomputes it. The resource atlas is rebuilt. Rerunning on the committed art
changes nothing.

Run it last, after anything that rewrites base layers:

    python3 tools/artwork/sharpen_upscales.py   # if rerun: rewrites sharpened bases
    python3 tools/artwork/ripen_wheat.py        # if rerun: rewrites the ripe wheat
    python3 tools/artwork/cast_shadows.py       # de-shadow, cast, register, atlas
    python3 tools/artwork/package_runtime.py --check
    python3 tools/artwork/runtime_provenance.py
    python3 tools/artwork/validate_runtime.py

    python3 tools/artwork/cast_shadows.py --review artifacts/shadows  # review sheets only
    python3 tools/artwork/cast_shadows.py --frames inn2b0 --review artifacts/shadows

Deterministic, without AI. Needs NumPy, SciPy and Pillow.
"""
import argparse
import hashlib
import io
import json
import math
import multiprocessing
import os
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[2]
SPECS = Path("content-src/gfx/shadows/specs.json")
GFX = Path("content/data/gfx")
PACK = Path("content/data/highres/v1")
CATEGORY = "shadows"
RECIPE = "cast ground shadow v1"
SCALE = 4
STEP = 4  # shadow canvases grow in whole steps of this many native pixels
MARGIN = 256  # HD pixels of room for a shadow past the frame while casting
# Baked shadows: near-black, semi-transparent pixels.
STRIP_ALPHA, STRIP_DARK = 220, 45


def _sunlight():
    """The shadow direction and the explorers' flight height from src/render/Sunlight.h."""
    text = (ROOT / "src/render/Sunlight.h").read_text()
    sx, sy = (float(v) for v in re.search(r"shadowX\s*=\s*([\d.]+)f,\s*shadowY\s*=\s*([\d.]+)f", text).groups())
    flight = float(re.search(r"flightHeight\s*=\s*([\d.]+)f", text).group(1))
    norm = math.hypot(sx, sy)
    return (sx / norm, sy / norm), flight


SHADOW, FLIGHT_HEIGHT = _sunlight()
# Building shadow length: screen px of throw per screen px of height. Specs set
# it per building as art direction (`length`), so small buildings read as low
# and towers as tall; see docs/assets/lighting.md for the sun elevations.
SHADOW_LENGTH = 0.6
MESH = Path("content-src/gfx/shadows/mesh")  # mesh_shadows.py masks and records
DEFAULTS = {
    "ratio": 1.1,          # block and contact throw per unit of height (boulders in terrain_decor use 1.15)
    "length": SHADOW_LENGTH,  # mesh: throw per unit of height, cast by mesh_shadows.py
    "mesh": None,          # mesh: the frame whose mask this one shares (damaged variants, or another
                           # level's); frames of other sizes align bottom-right, as buildings anchor
    "mesh_scale": 1.0,     # mesh: the mask scaled about the building's base (a borrowed shape, resized)
    "mesh_smooth": 0,      # mesh: radius (HD px) of an opening that removes thin parts poking out
    "proxy": None,         # proxy: a hand-built stand-in shape (see proxy_shadow), cast at `length`
    "cap": 180,            # longest throw, HD px: 45 native, under the 52 px flight height
    "blur": 2.0,           # softening of block shadows, HD px
    "body_alpha": 200,     # base pixels at least this opaque cast
    "team_alpha": 20,      # so do team-layer pixels this opaque (translucent blades)
    "height": 0, "blocks": [], "spires": [],
    "strip": True,
    "contact": 10,         # canopy: trunk contact shadow height, HD px
    "paint_out": None,     # an old shadow painted opaque into the art: relit away (paint_out)
}
PAINT_OUT = {  # the `relight` paint-out's detection and relighting, HD px
    "alpha": 220, "dark": 75, "chroma": 40,                # opaque, dark, grey: the painted shadow
    "open": 2, "min_area": 1500,                            # ...in large connected regions
    "lit_alpha": 200, "lit_bright": 170, "lit_chroma": 90,  # the lit ground it lies on
    "contrast": 0.9, "local": 40, "lit_match": 45,         # keep 90% of its texture; lit colour within ~40 px
    "dilate": 2, "feather": 2.5, "gain": 1.6,              # soft edge
}


def file_sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_specs(root=ROOT, path=None):
    """{frame id: spec} with the defaults filled in, in file order."""
    document = json.loads(Path(path or root / SPECS).read_text())
    if document.get("version") != 1:
        raise ValueError("Unknown shadow spec version")
    defaults = dict(DEFAULTS, **document.get("defaults", {}))
    specs = {}
    for family in document["families"]:
        unknown = set(family) - set(DEFAULTS) - {"frames", "method", "note"}
        if family.get("paint_out") and family["paint_out"].get("method") != "relight":
            raise ValueError("Unknown paint-out method: " + str(family["paint_out"].get("method")))
        if unknown:
            raise ValueError(f"Unknown shadow spec fields: {sorted(unknown)}")
        if family["method"] not in ("block", "canopy", "mesh", "proxy"):
            raise ValueError("Unknown shadow method: " + family["method"])
        for frame in family["frames"]:
            if frame in specs:
                raise ValueError("Duplicate shadow spec: " + frame)
            spec = dict(defaults, **{k: v for k, v in family.items() if k not in ("frames", "note")})
            if spec["cap"] > FLIGHT_HEIGHT * SCALE:
                raise ValueError(f"{frame}: shadow cap above the flight height")
            if spec["method"] == "mesh":
                spec["mesh"] = spec["mesh"] or frame
            specs[frame] = spec
    return specs


def strips(frame_id, root=ROOT):
    """Whether this frame's base layers are de-shadowed (validate_runtime.py)."""
    spec = load_specs(root).get(frame_id)
    return bool(spec and spec["strip"])


def strip_mask(rgba):
    """The baked-shadow pixels of an RGBA array."""
    alpha = rgba[..., 3]
    return (alpha > 0) & (alpha < STRIP_ALPHA) & (rgba[..., :3].max(-1) < STRIP_DARK)


def paint_out_mask(rgba, params):
    """The opaque painted-shadow pixels: dark, grey, in large connected regions."""
    p = dict(PAINT_OUT, **params)
    rgb = rgba[..., :3].astype(int)
    chroma = rgb.max(-1) - rgb.min(-1)
    mask = (rgba[..., 3] >= p["alpha"]) & (rgb.max(-1) < p["dark"]) & (chroma < p["chroma"])
    mask = ndimage.binary_opening(mask, iterations=p["open"])
    labels, count = ndimage.label(mask)
    sizes = ndimage.sum(mask, labels, range(1, count + 1))
    return np.isin(labels, 1 + np.nonzero(sizes > p["min_area"])[0])


def relight(rgba, mask, params, scale=1):
    """Relight `mask` (HD px / scale for a classic frame) to the lit ground around it."""
    p = dict(PAINT_OUT, **params)
    if not mask.any():
        return rgba
    out = rgba.astype(float)
    rgb = out[..., :3]
    chroma = rgb.max(-1) - rgb.min(-1)
    lit = (rgba[..., 3] > p["lit_alpha"]) & (rgb.max(-1) > p["lit_bright"]) & (chroma < p["lit_chroma"]) & ~mask
    if not lit.any():
        return rgba
    # The local lit colour comes only from ground close to the typical lit colour,
    # not from the building beside it.
    lit &= np.abs(rgb - rgb[lit].mean(0)).max(-1) < p["lit_match"]
    sigma = p["local"] / scale
    weight = ndimage.gaussian_filter(lit.astype(float), sigma)
    local = np.stack([ndimage.gaussian_filter(rgb[..., c] * lit, sigma) for c in range(3)], -1)
    local = np.where(weight[..., None] > 1e-3, local / np.maximum(weight[..., None], 1e-9), rgb[lit].mean(0))
    shadow_mean, shadow_std = rgb[mask].mean(0), rgb[mask].std(0) + 1e-6
    relit = (rgb - shadow_mean) / shadow_std * rgb[lit].std(0) * p["contrast"] + local
    # How far into the shadow each pixel is: lit pixels at the edge stay as they are.
    luma, local_luma = rgb.mean(-1), local.mean(-1)
    darkness = np.clip((local_luma - luma) / np.maximum(local_luma - shadow_mean.mean(), 1.0), 0, 1)
    darkness[mask] = 1.0
    edge = ndimage.binary_dilation(mask, iterations=max(1, round(p["dilate"] / scale)))
    blend = np.clip(ndimage.gaussian_filter(edge.astype(float), p["feather"] / scale) * p["gain"], 0, 1) * darkness
    blend[rgba[..., 3] == 0] = 0
    out[..., :3] = rgb + blend[..., None] * (relit - rgb)
    return np.clip(np.rint(out), 0, 255).astype(np.uint8)


def deshadow(hd, classic, spec):
    """The bases without their old shadows: baked semi-transparent ones stripped,
    and for a `paint_out` spec the painted opaque one relit (classic from the HD
    mask area-averaged). Idempotent."""
    if spec.get("strip"):
        hd = strip(hd)
        classic = strip(classic) if classic is not None else None
    if spec.get("paint_out"):
        params = {k: v for k, v in spec["paint_out"].items() if k != "method"}
        mask = paint_out_mask(hd, params)
        hd = relight(hd, mask, params)
        if classic is not None:
            small = downscale(mask.astype(np.uint8) * 255) >= 64  # any quarter of a classic pixel
            classic = relight(classic, small, params, SCALE)
    return hd, classic


def strip(image):
    """The image without its baked shadow (idempotent); a PIL image or RGBA array."""
    array = np.array(image.convert("RGBA") if isinstance(image, Image.Image) else image, np.uint8)
    array[strip_mask(array), 3] = 0
    return Image.fromarray(array, "RGBA") if isinstance(image, Image.Image) else array


# --- casting ---------------------------------------------------------------

def footprint(body, height):
    """Silhouette pixels with `height` more silhouette pixels straight above them:
    in the 3/4 view a block of that height covers its footprint swept up by it."""
    height = int(round(height))
    if height <= 0:
        return body.copy()
    run = np.zeros(body.shape, np.int32)
    for y in range(body.shape[0]):
        run[y] = (run[y - 1] + 1) * body[y] if y else body[y]
    return run > height


def sweep(mask, length):
    """The union of the mask moved 0..length along the sun."""
    steps = int(max(abs(length * SHADOW[0]), abs(length * SHADOW[1]))) + 1
    out = np.zeros(mask.shape, bool)
    h, w = mask.shape
    for t in np.linspace(0, 1, steps + 1):
        dx, dy = int(round(t * length * SHADOW[0])), int(round(t * length * SHADOW[1]))
        out[dy:, dx:] |= mask[:h - dy, :w - dx]
    return out


def cast_segments(heights, mask, ratio, cap):
    """Each mask pixel of height h casts a segment from its ground point (h below
    it) h * ratio (at most cap) along the sun."""
    h, w = mask.shape
    ys, xs = np.nonzero(mask & (heights > 0.5))
    acc = np.zeros((h, w), bool)
    if not len(xs):
        return acc
    height = heights[ys, xs]
    length = np.minimum(height * ratio, cap)
    gx, gy = xs.astype(float), ys + height
    tx, ty = gx + length * SHADOW[0], gy + length * SHADOW[1]
    for t in np.linspace(0, 1, int(max(8, length.max() / 2))):
        px = np.rint(gx + (tx - gx) * t).astype(int)
        py = np.rint(gy + (ty - gy) * t).astype(int)
        ok = (px >= 0) & (px < w) & (py >= 0) & (py < h)
        acc[py[ok], px[ok]] = True
    return ndimage.binary_closing(acc, iterations=1)


def block_shadow(body, spec):
    """Blocks with their own heights, the rest at `height`, and spires."""
    yy = np.mgrid[:body.shape[0], :body.shape[1]][0]
    spire = np.zeros_like(body)
    heights = np.zeros(body.shape)
    for x0, y0, x1, y1, base_y, base_h in spec["spires"]:
        box = np.zeros_like(body)
        box[y0:y1, x0:x1] = True
        part = box & body
        spire |= part
        heights[part] = np.maximum(heights[part], np.clip(base_y - yy[part], 0, None) + base_h)
    main = body & ~spire
    claimed = np.zeros_like(body)
    parts = []
    for x0, y0, x1, y1, height in spec["blocks"]:
        box = np.zeros_like(body)
        box[y0:y1, x0:x1] = True
        part = main & box & ~claimed
        claimed |= part
        parts.append((height, part))
    parts.append((spec["height"], main & ~claimed))
    shadow = np.zeros_like(body)
    for height, part in parts:
        if part.any():
            shadow |= sweep(footprint(part, height), height * min(spec["ratio"], spec["cap"] / max(height, 1)))
    if spire.any():
        shadow |= cast_segments(heights, spire, spec["ratio"], spec["cap"])
    return ndimage.gaussian_filter(shadow.astype(float), spec["blur"])


def canopy_shadow(body, spec):
    """Each canopy pixel lands `ratio` times its height above the trunk base
    along the sun; blurred, with a contact shadow at the trunk."""
    h, w = body.shape
    heights = np.zeros((h, w))
    for x in range(w):
        ys = np.nonzero(body[:, x])[0]
        if len(ys):
            heights[ys, x] = ys.max() - ys
    ys, xs = np.nonzero(body)
    height = heights[ys, xs]
    px = np.rint(xs + height * spec["ratio"] * SHADOW[0]).astype(int)
    py = np.rint(ys + height + height * spec["ratio"] * SHADOW[1]).astype(int)
    ok = (px >= 0) & (px < w) & (py >= 0) & (py < h)
    projected = np.zeros((h, w), bool)
    projected[py[ok], px[ok]] = True
    projected = ndimage.binary_closing(projected, iterations=3)
    soft = np.clip(ndimage.gaussian_filter(projected.astype(float), spec["blur"]) * 1.15, 0, 1)
    contact = cast_segments(np.minimum(heights, spec["contact"]), body, DEFAULTS["ratio"],
                            spec["contact"] * DEFAULTS["ratio"])
    return np.maximum(soft, ndimage.gaussian_filter(contact.astype(float), 2.0))


def frustum_mask(frustum, shape, steps=64):
    """The screen silhouette of a tapered cylinder: its rim and ground ellipses
    and every ellipse between them."""
    tx, ty, trx, try_, bx, by, brx, bry = frustum
    yy, xx = np.mgrid[0:shape[0], 0:shape[1]]
    out = np.zeros(shape, bool)
    for t in np.linspace(0, 1, steps):
        cx, cy = bx + (tx - bx) * t, by + (ty - by) * t
        rx, ry = max(brx + (trx - brx) * t, 1), max(bry + (try_ - bry) * t, 1)
        out |= ((xx - cx) / rx) ** 2 + ((yy - cy) / ry) ** 2 <= 1
    return out


VIEW_ELEVATION_DEG = 59.6  # the sprites' shared 3/4 view (mesh_shadows.ELEVATION_DEG)


def fit_frustum(body, start):
    """The round tapered cylinder whose silhouette best overlaps the body
    (intersection over union). Seen at the sprites' view, a circle shows as an
    ellipse sin(elevation) as tall as it is wide, so each ellipse has only a
    centre and a width; the best of a few starts round the guess is kept."""
    from scipy import optimize
    k = math.sin(math.radians(VIEW_ELEVATION_DEG))
    small = body[::2, ::2]

    def full(q):
        tx, ty, trx, bx, by, brx = q
        return [tx, ty, trx, trx * k, bx, by, brx, brx * k]

    def loss(q):
        m = frustum_mask(np.asarray(full(q)) / 2, small.shape, 32)
        return -(m & small).sum() / max((m | small).sum(), 1)
    tx, ty, trx, _, bx, by, brx, _ = start
    best = None
    for dy in (-12, 0, 12):
        guess = np.array([tx, ty + dy, trx, bx, by - dy, brx], float)
        result = optimize.minimize(loss, guess, method="Nelder-Mead",
                                   options={"xatol": 0.5, "fatol": 1e-4, "maxiter": 4000})
        if best is None or result.fun < best.fun:
            best = result
    return [round(float(v), 1) for v in full(best.x)], -best.fun


def banner_ground(frustum, box, team_body):
    """The ground row under a banner: where its lowest pixel meets the rim, at the
    same angle round the ground ellipse."""
    tx, ty, trx, try_, bx, by, brx, bry = frustum
    x0, y0, x1, y1 = box
    py, px = np.nonzero(team_body[y0:y1, x0:x1])
    low = py.max()
    lx = px[py >= low - 2].mean() + x0
    theta = math.atan2((low + y0 - ty) / try_, (lx - tx) / trx)
    return by + bry * math.sin(theta)


def proxy_points(proxy, team_body):
    """(x, y, H) screen points of a proxy shape, H screen px above the ground.

    `frustum`: [top cx, cy, rx, ry, bottom cx, cy, rx, ry], the rim and ground
    ellipses of a tapered cylinder as the sprite shows them (fit them with
    --fit-proxy). `banners`: boxes [x0, y0, x1, y1] of team-layer pixels held up
    on the rim; each stands on the ground ellipse below where its lowest pixel
    meets the rim (banner_ground), so a pixel at row y is ground - y high."""
    xs, ys, hs = [], [], []
    frustum = proxy["frustum"]
    tx, ty, trx, try_, bx, by, brx, bry = frustum
    height = by - ty
    for t in np.linspace(0, 1, max(2, int(height) + 1)):
        cx, cy = bx + (tx - bx) * t, by + (ty - by) * t
        rx, ry = brx + (trx - brx) * t, bry + (try_ - bry) * t
        if t == 1:  # the rim's top face, filled
            ex, ey = np.meshgrid(np.arange(-rx, rx + 1), np.arange(-ry, ry + 1))
            inside = (ex / rx) ** 2 + (ey / ry) ** 2 <= 1
            ex, ey = ex[inside], ey[inside]
        else:
            theta = np.linspace(0, 2 * np.pi, int(4 * max(rx, 1) * np.pi))
            ex, ey = rx * np.cos(theta), ry * np.sin(theta)
        xs.append(cx + ex); ys.append(cy + ey); hs.append(np.full(ex.shape, height * t))
    for box in proxy.get("banners", []):
        x0, y0, x1, y1 = box
        py, px = np.nonzero(team_body[y0:y1, x0:x1])
        if not len(py):  # a banner this frame has lost
            continue
        ground = banner_ground(frustum, box, team_body)
        py, px = py + y0, px + x0
        xs.append(px.astype(float)); ys.append(py.astype(float)); hs.append(np.maximum(0, ground - py).astype(float))
    return np.concatenate(xs), np.concatenate(ys), np.concatenate(hs)


def proxy_shadow(canvas, team_body, spec):
    """The shadow of a proxy shape: every point thrown from its ground point
    min(length * H, cap) along the sun (no columns: high parts cast apart),
    filled, merged with the shape's ground footprint and softened."""
    xs, ys, hs = proxy_points(spec["proxy"], team_body)
    throw = np.minimum(hs * spec["length"], spec["cap"])
    gx, gy = xs + throw * SHADOW[0], ys + hs + throw * SHADOW[1]
    h, w = canvas.shape
    out = np.zeros((h, w), bool)
    ix, iy = np.rint(gx).astype(int), np.rint(gy).astype(int)
    ok = (ix >= 0) & (ix < w) & (iy >= 0) & (iy < h)
    out[iy[ok], ix[ok]] = True
    _, _, _, _, bx, by, brx, bry = spec["proxy"]["frustum"]  # the footprint itself
    yy, xx = np.mgrid[0:h, 0:w]
    out |= ((xx - bx) / brx) ** 2 + ((yy - by) / bry) ** 2 <= 1
    out = ndimage.binary_fill_holes(ndimage.binary_closing(out, iterations=4) | out)
    return ndimage.gaussian_filter(out.astype(float), spec["blur"])


def cast(base, team, spec):
    """(HD shadow alpha, classic shadow alpha) as uint8 arrays for a de-shadowed
    HD base (and optional team layer) at SCALE x the classic frame."""
    body = base[..., 3] >= spec["body_alpha"]
    if team is not None:
        # A team layer shares the base's top-left corner; it may be smaller.
        h, w = min(team.shape[0], body.shape[0]), min(team.shape[1], body.shape[1])
        body[:h, :w] |= team[:h, :w, 3] >= spec["team_alpha"]
    h0, w0 = body.shape
    canvas = np.zeros((h0 + MARGIN, w0 + MARGIN), bool)
    canvas[:h0, :w0] = body
    if spec["method"] == "proxy":
        team_body = np.zeros(canvas.shape, bool)
        if team is not None:
            th, tw = min(team.shape[0], h0), min(team.shape[1], w0)
            team_body[:th, :tw] = team[:th, :tw, 3] >= spec["team_alpha"]
        mask = proxy_shadow(canvas, team_body, spec)
    else:
        mask = (canopy_shadow if spec["method"] == "canopy" else block_shadow)(canvas, spec)
    ys, xs = np.nonzero(mask > 0.5 / 255)
    unit = STEP * SCALE
    right = -(-max(0, (xs.max() + 1 if len(xs) else 0) - w0) // unit) * unit
    bottom = -(-max(0, (ys.max() + 1 if len(ys) else 0) - h0) // unit) * unit
    hd = np.rint(np.clip(mask[:h0 + bottom, :w0 + right], 0, 1) * 255).astype(np.uint8)
    return hd, downscale(hd)


def mesh_record(spec, root=ROOT):
    """The committed mesh_shadows.py record of a mesh spec, checked against it."""
    path = root / MESH / f"{spec['mesh']}.json"
    if not path.exists():
        raise ValueError(f"{spec['mesh']}: no mesh shadow in {MESH}; run tools/artwork/mesh_shadows.py")
    record = json.loads(path.read_text())
    if record["shadow"]["length"] != spec["length"]:
        raise ValueError(f"{spec['mesh']}: its mesh shadow was cast at length {record['shadow']['length']}, "
                         f"the spec says {spec['length']}; rerun tools/artwork/mesh_shadows.py")
    return record


def mesh_body(hd, team, spec):
    """A frame's silhouette as a mesh spec reads it: opaque base or team pixels."""
    body = hd[..., 3] >= spec["body_alpha"]
    if team is not None:
        body[:team.shape[0], :team.shape[1]] |= team[:body.shape[0], :body.shape[1], 3] >= spec["team_alpha"]
    return body


def mesh_layer(spec, shape, root=ROOT, body=None):
    """The HD shadow alpha of a mesh spec: its committed mask (the frame's top-left
    corner), on a canvas covering the frame, padded right and bottom to whole steps."""
    mesh_record(spec, root)
    mask = _open(root / MESH / f"{spec['mesh']}s.png")[..., 3]
    with Image.open(root / PACK / f"{spec['mesh']}.png") as source:
        sw, sh = source.size
    # A building draws with its bottom-right corner on its footprint's, so another
    # frame's mask moves by the difference in size (its top-left may fall outside).
    dy, dx = shape[0] - sh, shape[1] - sw
    if dy or dx:
        moved = np.zeros((max(0, mask.shape[0] + dy), max(0, mask.shape[1] + dx)), mask.dtype)
        src = mask[max(0, -dy):, max(0, -dx):]
        moved[max(0, dy):max(0, dy) + src.shape[0], max(0, dx):max(0, dx) + src.shape[1]] = src
        mask = moved
    out = mesh_pad(mask, *shape)
    if spec["mesh_scale"] != 1.0:
        # Scaled about the base of the building (the mean of its lowest 15% of
        # rows), where its shadow meets it, so it shrinks toward the building.
        if body is None:
            raise ValueError("a scaled mesh mask needs the frame's silhouette")
        by, bx = np.nonzero(body)
        low = by >= np.percentile(by, 85)
        ay, ax, f = by[low].mean(), bx[low].mean(), spec["mesh_scale"]
        out = np.rint(np.clip(ndimage.affine_transform(out.astype(float), [1 / f, 1 / f],
                                                       offset=[ay - ay / f, ax - ax / f], order=1), 0, 255)
                      ).astype(np.uint8)
    if body is not None and (dy or dx or spec["mesh_scale"] != 1.0):
        # Another frame's mask keeps only what this frame's own silhouette can
        # throw along the sun: none of it shows beside the building up or left.
        canvas = np.zeros(out.shape, bool)
        canvas[:body.shape[0], :body.shape[1]] = body
        reach = ndimage.binary_dilation(sweep(canvas, spec["cap"]), iterations=int(2 * spec["blur"]) + 1)
        out = np.where(reach, out, 0).astype(out.dtype)
    if spec["mesh_smooth"]:
        r = int(spec["mesh_smooth"])
        yy, xx = np.mgrid[-r:r + 1, -r:r + 1]
        out = ndimage.grey_opening(out, footprint=xx * xx + yy * yy <= r * r).astype(np.uint8)
    return out


def mesh_pad(mask, h0, w0):
    """A mask at the frame's top-left corner on the frame's canvas, padded right and bottom to whole steps."""
    unit = STEP * SCALE
    right = -(-max(0, mask.shape[1] - w0) // unit) * unit
    bottom = -(-max(0, mask.shape[0] - h0) // unit) * unit
    out = np.zeros((h0 + bottom, w0 + right), np.uint8)
    h, w = min(mask.shape[0], out.shape[0]), min(mask.shape[1], out.shape[1])
    out[:h, :w] = mask[:h, :w]
    return out


def downscale(hd):
    """The classic shadow: the HD alpha area-averaged SCALE x SCALE, rounded."""
    h, w = hd.shape
    blocks = hd.reshape(h // SCALE, SCALE, w // SCALE, SCALE).astype(np.int64).sum((1, 3))
    return ((blocks * 2 + SCALE * SCALE) // (2 * SCALE * SCALE)).astype(np.uint8)


def shadow_image(alpha):
    out = np.zeros(alpha.shape + (4,), np.uint8)
    out[..., 3] = alpha
    return Image.fromarray(out, "RGBA")


# --- frames ------------------------------------------------------------------

def _open(path):
    with Image.open(path) as image:
        return np.asarray(image.convert("RGBA"))


def _png(image):
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", optimize=False)
    return buffer.getvalue()


def compute(job):
    """One frame: (frame id, {"hd", "classic": de-shadowed bases, "hd_shadow",
    "classic_shadow": shadow alphas}) as arrays."""
    frame_id, spec, root, inputs = job
    root = Path(root)
    hd = inputs.get("hd") if inputs.get("hd") is not None else _open(root / PACK / f"{frame_id}.png")
    classic = inputs.get("classic") if inputs.get("classic") is not None else _open(root / GFX / f"{frame_id}.png")
    team_path = root / PACK / f"{frame_id}r.png"
    team = _open(team_path) if team_path.exists() else None
    if inputs.get("classic_stripped"):
        hd = deshadow(hd, None, spec)[0]
    else:
        hd, classic = deshadow(hd, classic, spec)
    if hd.shape[:2] != (classic.shape[0] * SCALE, classic.shape[1] * SCALE):
        raise ValueError(f"{frame_id}: HD base is not {SCALE}x the classic frame")
    if spec["method"] == "mesh":
        hd_shadow = mesh_layer(spec, hd.shape[:2], root, mesh_body(hd, team, spec))
        classic_shadow = downscale(hd_shadow)
    else:
        hd_shadow, classic_shadow = cast(hd, team, spec)
    return frame_id, {"hd": hd, "classic": classic, "hd_shadow": hd_shadow, "classic_shadow": classic_shadow}


def run(frame_ids, specs, root=ROOT, jobs=None):
    """{frame id: computed layers} for these frames, the ripe wheat after its
    stage-4 frame (re-ripened from it with ripen_wheat.ripen)."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import ripen_wheat
    ripe = {f"ressource{r}": f"ressource{s}" for r, s in ripen_wheat.RIPE.items()}
    first = [f for f in frame_ids if f not in ripe]
    with multiprocessing.get_context("spawn").Pool(jobs or os.cpu_count()) as pool:
        results = dict(pool.map(compute, [(f, specs[f], str(root), {}) for f in first], chunksize=1))
        second = []
        for frame_id in frame_ids:
            if frame_id not in ripe:
                continue
            stage4 = ripe[frame_id]
            if stage4 not in results:
                raise ValueError(f"{frame_id} needs its stage-4 frame {stage4} in the same run")
            # The ripe sprite has the stage-4 shape (ripen_wheat requires it): both
            # lose the baked-shadow pixels of either.
            classic = np.array(_open(root / GFX / f"{frame_id}.png"))
            if specs[frame_id]["strip"]:
                stage4_classic = np.array(results[stage4]["classic"])
                both = strip_mask(_open(root / GFX / f"{stage4}.png")) | strip_mask(classic)
                classic[both, 3] = 0
                stage4_classic[both, 3] = 0
                results[stage4] = dict(results[stage4], classic=stage4_classic)
            hd = np.asarray(ripen_wheat.ripen(Image.fromarray(results[stage4]["hd"]),
                                              Image.fromarray(results[stage4]["classic"]),
                                              Image.fromarray(classic)))
            second.append((frame_id, specs[frame_id], str(root),
                           {"hd": hd, "classic": classic, "classic_stripped": True}))
        results.update(pool.map(compute, second, chunksize=1))
    return results


# --- registration ---------------------------------------------------------------

def _source(path, root):
    return {"path": str(Path(path).relative_to(root)) if Path(path).is_absolute() else str(path),
            "sha256": file_sha(root / path)}


def write(results, specs, root=ROOT):
    """Write layers and register them in both pack copies; returns changed paths."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import highres_pack
    production = root / highres_pack.PRODUCTION
    runtime = root / highres_pack.RUNTIME
    manifest = json.loads((production / "pack-metadata/manifest.json").read_text())
    rows = highres_pack._read_rows(production / "pack-metadata/frames.txt")
    index = json.loads((production / "package.json").read_text())
    records = {record["runtime"]: record for record in index["files"]}
    frames = {frame["id"]: frame for frame in manifest["frames"]}
    changed = []

    def put(path, data):
        if not path.exists() or path.read_bytes() != data:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            changed.append(str(path.relative_to(root)))

    # De-shadowed bases first: their hashes are the shadows' sources. Rewrite
    # only changed pixels, so committed encodings stay as they are.
    renamed = {}
    for frame_id, layers in results.items():
        classic = root / GFX / f"{frame_id}.png"
        if not np.array_equal(_open(classic), layers["classic"]):
            before = file_sha(classic)
            put(classic, _png(Image.fromarray(layers["classic"], "RGBA")))
            renamed[str(GFX / f"{frame_id}.png")] = before
        name = f"{frame_id}.png"
        if not np.array_equal(_open(runtime / name), layers["hd"]):
            data = _png(Image.fromarray(layers["hd"], "RGBA"))
            put(production / records[name]["source"], data)
            put(runtime / name, data)
            digest = hashlib.sha256(data).hexdigest()
            records[name]["sha256"] = digest
            for layer in frames[frame_id]["layers"]:
                if layer["file"] == name:
                    layer["sha256"] = digest
    # Sources naming a rewritten classic sprite (the ripe wheat's) follow it.
    for frame in manifest["frames"]:
        for source in frame.get("sources", []):
            if source["path"] in renamed:
                source["sha256"] = file_sha(root / source["path"])

    tool = {"path": "tools/artwork/cast_shadows.py", "sha256": file_sha(Path(__file__).resolve())}
    for frame_id, layers in results.items():
        shadow_id = f"{frame_id}s"
        name = f"{shadow_id}.png"
        classic_data = _png(shadow_image(layers["classic_shadow"]))
        hd_data = _png(shadow_image(layers["hd_shadow"]))
        put(root / GFX / name, classic_data)
        relative = f"{CATEGORY}/{name}"
        put(production / relative, hd_data)
        put(runtime / name, hd_data)
        digest = hashlib.sha256(hd_data).hexdigest()
        height, width = layers["classic_shadow"].shape
        spec = specs[frame_id]
        sources = [tool, _source(SPECS, root), _source(PACK / f"{frame_id}.png", root)]
        if spec["method"] == "mesh":
            sources += [_source(MESH / f"{spec['mesh']}s.png", root), _source(MESH / f"{spec['mesh']}.json", root)]
        if (runtime / f"{frame_id}r.png").exists():
            sources.append(_source(PACK / f"{frame_id}r.png", root))
        sources.append(_source(GFX / f"{frame_id}.png", root))
        entry = {
            "id": shadow_id, "width": width, "height": height, "scale": SCALE,
            "recipe": f"{RECIPE}: {spec['method']}",
            "layers": [{"file": name, "role": "base", "sha256": digest, "source_sha256": digest,
                        "logical_width": width, "logical_height": height,
                        "original_sha256": hashlib.sha256(classic_data).hexdigest()}],
            "sources": sources,
        }
        if shadow_id in frames:
            manifest["frames"][[f["id"] for f in manifest["frames"]].index(shadow_id)] = entry
        else:
            position = [f["id"] for f in manifest["frames"]].index(frame_id) + 1
            manifest["frames"].insert(position, entry)
        frames[shadow_id] = entry
        row = [shadow_id, str(width), str(height), str(SCALE), name, "-"]
        ids = [r[0] for r in rows]
        if shadow_id in ids:
            rows[ids.index(shadow_id)] = row
        else:
            rows.insert(ids.index(frame_id) + 1, row)
        record = records.get(name)
        if record is None:
            record = {"runtime": name, "source": relative, "sha256": digest}
            index["files"].append(record)
            records[name] = record
        record.update(source=relative, sha256=digest)
    before = {p: (root / p).read_bytes() for p in (highres_pack.PRODUCTION / "pack-metadata/manifest.json",
                                                   highres_pack.PRODUCTION / "pack-metadata/frames.txt",
                                                   highres_pack.PRODUCTION / "package.json")}
    highres_pack._save_metadata(manifest, rows, index, root)
    changed += [str(p) for p, data in before.items() if (root / p).read_bytes() != data]
    return changed


def prune(specs, root=ROOT):
    """Unregister shadow layers whose frames left the spec file."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import highres_pack
    manifest = json.loads((root / highres_pack.PRODUCTION / "pack-metadata/manifest.json").read_text())
    stale = [f["id"] for f in manifest["frames"]
             if f["recipe"].startswith(RECIPE) and f["id"][:-1] not in specs]
    if stale:
        highres_pack.unregister_frames(stale, root)
        for frame_id in stale:
            (root / GFX / f"{frame_id}.png").unlink(missing_ok=True)
    return stale


# --- review -----------------------------------------------------------------

def composite(base, team, shadow, scale, background=(88, 128, 64), opacity=None):
    """The frame on grass with its shadow at the game's opacity and red team colour."""
    if opacity is None:
        opacity = float(re.search(r"groundShadowOpacity\s*=\s*([\d.]+)f",
                                  (ROOT / "src/render/Sunlight.h").read_text()).group(1))
    h, w = shadow.shape
    canvas = np.zeros((h, w, 3))
    canvas[:] = background
    canvas *= (1 - opacity * shadow[..., None] / 255.0)
    for layer in ((base, False), (team, True)):
        image, recolour = layer
        if image is None:
            continue
        rgb = image[..., :3].astype(float)
        if recolour:
            luma = rgb.mean(-1, keepdims=True) / 255
            rgb = luma * np.array([225.0, 45.0, 40.0])
        ih, iw = min(image.shape[0], h), min(image.shape[1], w)
        rgb, a = rgb[:ih, :iw], image[:ih, :iw, 3:4] / 255.0
        canvas[:ih, :iw] = canvas[:ih, :iw] * (1 - a) + rgb * a
    out = Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8), "RGB")
    return out.resize((max(1, round(w * scale)), max(1, round(h * scale))), Image.Resampling.LANCZOS)


def review(results, specs, directory, root=ROOT):
    """One sheet per family: each frame's HD composite at game zoom 1 and 2, and classic at 2x."""
    from PIL import ImageDraw
    directory.mkdir(parents=True, exist_ok=True)
    families = {}
    for frame_id in results:
        family = re.sub(r"\d+$", "", frame_id) if not frame_id.startswith("ressource") else \
            f"ressource-{specs[frame_id]['method']}-{int(frame_id[9:]) // 10}"
        families.setdefault(family, []).append(frame_id)
    for family, frame_ids in families.items():
        tiles = []
        for frame_id in frame_ids:
            layers = results[frame_id]
            team_path = root / PACK / f"{frame_id}r.png"
            team = _open(team_path) if team_path.exists() else None
            classic_team_path = root / GFX / f"{frame_id}r.png"
            classic_team = _open(classic_team_path) if classic_team_path.exists() else None
            hd = composite(layers["hd"], team, layers["hd_shadow"], 1 / SCALE * 1)
            hd2 = composite(layers["hd"], team, layers["hd_shadow"], 2 / SCALE)
            classic = composite(layers["classic"], classic_team, layers["classic_shadow"], 1).resize(
                (hd2.width, hd2.height), Image.Resampling.NEAREST)
            tiles.append((frame_id, [hd, hd2, classic]))
        width = max(sum(t.width for t in images) + 8 * len(images) for _, images in tiles) + 8
        height = sum(max(t.height for t in images) + 20 for _, images in tiles) + 8
        sheet = Image.new("RGB", (width, height), (24, 24, 24))
        draw = ImageDraw.Draw(sheet)
        y = 4
        for frame_id, images in tiles:
            spec = specs[frame_id]
            draw.text((8, y), f"{frame_id}  {spec['method']} H={spec['height']} blocks={len(spec['blocks'])} "
                              f"spires={len(spec['spires'])}  [zoom 1 HD | zoom 2 HD | classic 2x]",
                      fill=(255, 255, 255))
            x, y = 8, y + 16
            for image in images:
                sheet.paste(image, (x, y))
                x += image.width + 8
            y += max(t.height for t in images) + 4
        sheet.save(directory / f"{family}.png")
    return sorted(families)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--frames", nargs="*", help="only these frames (default: every spec)")
    parser.add_argument("--review", type=Path, help="write review sheets here instead of the pack")
    parser.add_argument("--jobs", type=int, help="parallel frames (default: every CPU)")
    parser.add_argument("--fit-proxy", action="store_true",
                        help="refit each proxy spec's frustum to its frame's silhouette, print it and exit")
    args = parser.parse_args(argv)
    specs = load_specs()
    frame_ids = args.frames or list(specs)
    missing = [f for f in frame_ids if f not in specs]
    if missing:
        parser.error(f"no shadow spec for {missing}")
    for frame_id in frame_ids:
        if not (ROOT / GFX / f"{frame_id}.png").exists() or not (ROOT / PACK / f"{frame_id}.png").exists():
            parser.error(f"{frame_id}: no classic or HD base layer")
    if args.fit_proxy:
        for frame_id in frame_ids:
            if specs[frame_id]["method"] == "proxy":
                body = ndimage.binary_fill_holes(_open(ROOT / PACK / f"{frame_id}.png")[..., 3] >= specs[frame_id]["body_alpha"])
                frustum, overlap = fit_frustum(body, specs[frame_id]["proxy"]["frustum"])
                print(f"{frame_id}: frustum {frustum} (silhouette IoU {overlap:.3f})")
        return
    results = run(frame_ids, specs, jobs=args.jobs)
    if args.review:
        names = review(results, specs, args.review)
        print(f"Wrote {len(names)} review sheets to {args.review}")
        return
    if args.frames is None:
        stale = prune(specs)
        if stale:
            print(f"Unregistered {len(stale)} shadow layers without a spec")
    changed = write(results, specs)
    if any(f.startswith("ressource") for f in results):
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import highres_pack
        atlas = [highres_pack.PRODUCTION / "atlases" / f"ressource-atlas-mip{i}.png" for i in range(4)]
        before = {p: (ROOT / p).read_bytes() if (ROOT / p).exists() else b"" for p in atlas}
        highres_pack.rebuild_resource_atlas()
        changed += [str(p) for p, data in before.items() if (ROOT / p).read_bytes() != data]
    print(f"Cast {len(results)} shadow layers; {len(changed)} files changed")


if __name__ == "__main__":
    main()
