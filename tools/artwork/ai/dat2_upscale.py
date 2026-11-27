#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage transformer-SR HD candidates for AI-upscaled building frames; promote reviewed ones.

The pipeline is the reviewed bench variant `r2_agfl_sharp`; `VARIANTS` choose
its models (`--variant`). The approved reference `usv2-bhi` averages
4x-UltraSharpV2 and 4xBHI_dat2_real, but UltraSharpV2 is CC-BY-NC-SA, so that
variant is reference-only and can never be promoted. The others use only
CC-BY-4.0 models by Philip Hofmann: 4xBHI_dat2_real (bhi), 4xNomos8kDAT
(nomos) and 4xRealWebPhoto_v3_atd (atd), alone or averaged: `bhi`, `nomos`,
`atd`, `bhi-nomos`, `bhi-atd`, `nomos-atd`, `bhi-nomos-atd`. The silhouette
alpha comes from BHI where the variant includes it, else from its first model.

1. Inference (MPS or CPU, spandrel). Every classic layer (base, team, and the
   base+team composite in the classic team green) is filled under alpha 0,
   edge-padded 16 px and upscaled 4x by each of the variant's models, in two
   modes each:
   * `chain`: model 4x, area-resize to 2x, model again, area-resize to 4x;
   * `tta4`:  mean of the model over identity, h-flip, v-flip and rot180.
   The base/team alpha is also run through the variant's alpha model (chain) as grey.
   Outputs are quantised to float16, as in the reviewed bench run.
2. Assembly (CPU only, deterministic):
   * colour = mean of the models' `chain` runs; where the models disagree
     (largest pairwise smoothed luminance difference) or the classic is flat,
     blend toward the mean of their `tta4` runs (a single-model variant has no
     disagreement gate, as in the bench assembler: only the flat fallback);
   * alpha = model alpha on hard native alpha steps, classic bilinear elsewhere
     (soft shadows stay soft), never outside the classic footprint;
   * under the team layer, the base is guided-filtered by the composite's
     luminance;
   * the team layer is unmixed jointly from the composite run against that
     base, with its hue locked to the bicubic classic team layer (the game
     hue-shifts it);
   * a luminance-only (Lab L) thresholded unsharp mask and mild clarity;
   * 3 iterations of back-projection to the classic at strength 0.8, after
     sharpening, so the 4x area downscale stays faithful;
   * Lab-L floors near silhouette and team edges (no dark rims), and outside
     the team shape the base matches the classic base outside the team;
   * team colour on the alpha ramp is bled in from the solid interior.

Usage (inference needs the optional environment in requirements-dat2.txt):

    python tools/artwork/ai/dat2_upscale.py --variant bhi --all-buildings --classic-rev HEAD \\
        --models /path/to/models --output /tmp/glob2-dat2-candidates
    python tools/artwork/ai/dat2_upscale.py --variant bhi --frame inn2b0 --models DIR --output DIR
    python3 tools/artwork/ai/dat2_upscale.py --promote DIR --dry-run   # Pillow only
    python3 tools/artwork/ai/dat2_upscale.py --promote DIR [--only a,b] [--skip c,d]

Candidates go only to a fresh staging directory outside the repository or under
`artifacts/`; the result is published all-or-nothing after every frame
succeeds. Each frame gets `provenance/<frame>.json` (classic hashes, model
file sha256 and source URLs, package versions, recipe, candidate hashes).
Promotion never runs inference: it copies reviewed staged PNGs byte-for-byte
into the approved pack.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]

SCALE = 4
PAD = 16                       # native px of edge padding before inference
TILE, OVERLAP = 384, 32        # model tile size and blend overlap (model-input px)
TTA4 = ((0, 0), (0, 1), (2, 0), (2, 1))  # (rot90 k, h-flip): identity, hflip, vflip, rot180

MODELS = {
    '4x-UltraSharpV2': dict(
        file='4x-UltraSharpV2.safetensors',
        sha256='2b5db674aa8f3864a696dccd1d07d7cc0500fbf220f35044cd30eee9a7f971a7',
        url='https://huggingface.co/Kim2091/UltraSharpV2/resolve/main/4x-UltraSharpV2.safetensors',
        author='Kim2091', arch='DAT2', licence='CC-BY-NC-SA-4.0', commercial=False,
        licence_url='https://huggingface.co/Kim2091/UltraSharpV2'),
    '4xBHI_dat2_real': dict(
        file='4xBHI_dat2_real.safetensors',
        sha256='1f651fe8e2b7ad978eee0db9c8de89b7bbfd9d2d671ffababb9baf6e2d5b38f6',
        url='https://huggingface.co/Phips/4xBHI_dat2_real/resolve/main/4xBHI_dat2_real.safetensors',
        author='Philip Hofmann (Phips)', arch='DAT2', licence='CC-BY-4.0', commercial=True,
        licence_url='https://huggingface.co/Phips/4xBHI_dat2_real'),
    '4xNomos8kDAT': dict(
        file='4xNomos8kDAT.safetensors',
        sha256='1ddb1be06180daae8d583ef387c1020abed32085a0448bca624f4f4310949868',
        url='https://huggingface.co/Phips/4xNomos8kDAT/resolve/main/4xNomos8kDAT.safetensors',
        author='Philip Hofmann (Phips)', arch='DAT', licence='CC-BY-4.0', commercial=True,
        licence_url='https://huggingface.co/Phips/4xNomos8kDAT'),
    '4xRealWebPhoto_v3_atd': dict(
        file='4xRealWebPhoto_v3_atd.safetensors',
        sha256='1812b76bb92e0a1af04c3cf02b7bd192acbe35abb071442026bc3827033a5412',
        url='https://huggingface.co/Phips/4xRealWebPhoto_v3_atd/resolve/main/4xRealWebPhoto_v3_atd.safetensors',
        author='Philip Hofmann (Phips)', arch='ATD', licence='CC-BY-4.0', commercial=True,
        licence_url='https://huggingface.co/Phips/4xRealWebPhoto_v3_atd'),
}
ALPHA_MODEL = '4xBHI_dat2_real'

# Model sets. Every variant keeps the 4xBHI_dat2_real chain alpha and the
# assembly below unchanged; only the averaged colour sources differ.
VARIANTS = {
    'usv2-bhi': dict(name='dat2 chain ensemble v1', models=('4x-UltraSharpV2', '4xBHI_dat2_real')),  # reference
    'bhi': dict(name='dat2 bhi chain v1', models=('4xBHI_dat2_real',)),
    'bhi-nomos': dict(name='dat2 bhi nomos8k chain ensemble v1', models=('4xBHI_dat2_real', '4xNomos8kDAT')),
    'bhi-atd': dict(name='dat2 bhi atd chain ensemble v1', models=('4xBHI_dat2_real', '4xRealWebPhoto_v3_atd')),
    # Variants without BHI take the silhouette alpha from their own first model.
    'nomos': dict(name='dat2 nomos8k chain v1', models=('4xNomos8kDAT',), alpha='4xNomos8kDAT'),
    'atd': dict(name='dat2 atd chain v1', models=('4xRealWebPhoto_v3_atd',), alpha='4xRealWebPhoto_v3_atd'),
    'nomos-atd': dict(name='dat2 nomos8k atd chain ensemble v1', models=('4xNomos8kDAT', '4xRealWebPhoto_v3_atd'),
                      alpha='4xNomos8kDAT'),
    'bhi-nomos-atd': dict(name='dat2 bhi nomos8k atd chain ensemble v1',
                          models=('4xBHI_dat2_real', '4xNomos8kDAT', '4xRealWebPhoto_v3_atd')),
}


WEIGHTED_NAME = 'dat2 weighted chain ensemble v1'
MODEL_ALIASES = {'bhi': '4xBHI_dat2_real', 'nomos': '4xNomos8kDAT', 'atd': '4xRealWebPhoto_v3_atd',
                 'usv2': '4x-UltraSharpV2'}


def weighted_spec(weights):
    """Variant spec of a weighted ensemble: `{'bhi': 0.5, ...}` or 'bhi=0.5,nomos=0.3,atd=0.2'.

    Weights must be >= 0 and sum to 1. Zero-weight models are dropped entirely
    (no inference, no vote in the agreement gate). The alpha comes from BHI when
    it is used, else from the heaviest model.
    """
    if isinstance(weights, str):
        try:
            weights = {k.strip(): float(v) for k, v in (item.split('=') for item in weights.split(',') if item.strip())}
        except ValueError:
            raise ValueError(f'Weights must look like bhi=0.5,nomos=0.3,atd=0.2, not {weights!r}')
    full = {}
    for key, value in weights.items():
        name = MODEL_ALIASES.get(key, key)
        if name not in MODELS or name in full:
            raise ValueError(f'Unknown or repeated model in weights: {key!r}')
        if not value >= 0:
            raise ValueError(f'Negative weight for {key!r}')
        full[name] = float(value)
    if abs(sum(full.values()) - 1) > 1e-6:
        raise ValueError(f'Model weights must sum to 1, not {sum(full.values()):g}')
    used = tuple(m for m in MODELS if full.get(m, 0) > 0)  # canonical model order
    alpha = ALPHA_MODEL if ALPHA_MODEL in used else max(used, key=lambda m: full[m])
    return dict(name=WEIGHTED_NAME, models=used, alpha=alpha, weights={m: full[m] for m in used})


def resolve(variant):
    """A variant key from VARIANTS, or a spec dict such as weighted_spec() returns."""
    if isinstance(variant, dict):
        return variant
    if variant not in VARIANTS:
        raise ValueError(f'Unknown variant {variant!r}; choose from {sorted(VARIANTS)}')
    return VARIANTS[variant]


def variant_key(variant):
    return 'weighted' if isinstance(variant, dict) else variant


def alpha_model_of(variant):
    return resolve(variant).get('alpha', ALPHA_MODEL)


def variant_models(variant):
    """Every model a variant runs: its colour models, then its alpha model."""
    return tuple(dict.fromkeys(resolve(variant)['models'] + (alpha_model_of(variant),)))


def promotable(variant):
    """Only variants whose every model licence allows commercial use may ship."""
    return all(MODELS[m]['commercial'] for m in variant_models(variant))


def recipe_names(promotable_only=False):
    return {v['name'] for k, v in VARIANTS.items() if promotable(k) or not promotable_only} | {WEIGHTED_NAME}


def variant_recipe(variant):
    """The assembly recipe of a variant: RECIPE with its chain/tta4 colour sources."""
    spec = resolve(variant)
    models = spec['models']
    recipe = dict(RECIPE, sharp=tuple(m + '/chain' for m in models), smooth=tuple(m + '/tta4' for m in models),
                  alpha=alpha_model_of(variant) + '/chain')
    if 'weights' in spec:
        recipe['weights'] = tuple(spec['weights'][m] for m in models)
    return recipe


# Assembly parameters; names follow the bench's assemble2.py flags.
RECIPE = dict(
    sharp=('4x-UltraSharpV2/chain', '4xBHI_dat2_real/chain'),   # replaced per variant
    smooth=('4x-UltraSharpV2/tta4', '4xBHI_dat2_real/tta4'),
    alpha=ALPHA_MODEL + '/chain',
    agree=(0.05, 0.15, 0.8),        # |L(M1)-L(M2)| gauss 1.5: smoothstep lo, hi, max smooth weight
    flat=(0.08, 0.2, 0.8),          # base only: classic structure lo, hi, max smooth weight
    base_team_guided=(3, 0.0003, 0.05, 0.25),  # radius, eps, team-alpha smoothstep lo, hi (guide: composite L)
    lusm=(1.9, 0.9, 1.5, 0.45),     # amount, sigma, threshold (L units), undershoot scale
    team_lusm=(1.9, 0.9, 1.2, 0.5),
    clarity=(0.25, 8.0),            # amount, sigma; midtone weighted
    backproject=(3, 0.8),           # iterations, strength; after sharpening
    rim_clamp=0.92, rim_px=3,       # L >= f * native 3x3 min L near alpha/team edges, after back-projection
    outside_clamp=0.9,              # base just outside the team shape vs the classic base outside the team
    team_bleed=2.0,                 # gaussian sigma of the interior colour bled onto the team alpha ramp
    cache_dtype='float16',
)

PRODUCTION = Path('content-src/gfx/production')
RUNTIME = Path('content/data/highres/v1')
CLASSIC = Path('content/data/gfx')
EXCLUDED_PREFIXES = ('wall', 'buildingsite')  # connected tiles (seams) / construction sites


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def cpu_count():
    return os.cpu_count() or 1


# ---------------------------------------------------------------- frame selection
def ai_recipes():
    sys.path.insert(0, str(ROOT))
    from tools.artwork.package_runtime import AI
    return set(AI) | recipe_names()


def load_manifest(root):
    return json.loads((root / PRODUCTION / 'pack-metadata/manifest.json').read_text())


def building_sprites(root):
    """Sprite prefixes (`hosp0b`, `wall`, ...) named by `gameSprite` in the building types."""
    found = set()

    def walk(node):
        if isinstance(node, dict):
            for key, value in node.items():
                if key == 'gameSprite' and isinstance(value, str):
                    found.add(value.rsplit('/', 1)[-1])
                walk(value)
        elif isinstance(node, list):
            for value in node:
                walk(value)
    for path in sorted((root / 'content/data/buildings').glob('*.json')):
        walk(json.loads(path.read_text()))
    return found


def sprite_of(frame_id, prefixes):
    return next((p for p in sorted(prefixes, key=len, reverse=True)
                 if re.fullmatch(re.escape(p) + r'\d+', frame_id)), None)


def all_building_frames(root):
    """AI-upscaled building frames (finished, construction, variants), minus walls/building sites."""
    prefixes = building_sprites(root)
    recipes = ai_recipes()
    out = []
    for frame in load_manifest(root)['frames']:
        prefix = sprite_of(frame['id'], prefixes)
        if prefix and frame['recipe'] in recipes and not prefix.startswith(EXCLUDED_PREFIXES):
            out.append(frame['id'])
    return out


class ClassicSource:
    """Classic layer bytes from the working tree, or from a Git revision with `rev`."""

    def __init__(self, root, rev=None):
        self.root, self.rev = Path(root), rev
        if rev:
            import subprocess
            self.commit = subprocess.run(['git', 'rev-parse', '--verify', rev + '^{commit}'], cwd=self.root,
                                         check=True, capture_output=True, text=True).stdout.strip()

    def read(self, name):
        """Bytes of content/data/gfx/<name>, or None if it does not exist."""
        if not self.rev:
            path = self.root / CLASSIC / name
            return path.read_bytes() if path.is_file() else None
        import subprocess
        result = subprocess.run(['git', 'show', f'{self.commit}:{(CLASSIC / name).as_posix()}'], cwd=self.root,
                                capture_output=True)
        return result.stdout if result.returncode == 0 else None


def validate_frame(root, frame_id, manifest=None, source=None):
    """Manifest entry of an AI building frame whose classic layers match it; raises ValueError."""
    if not re.fullmatch(r'[A-Za-z0-9_-]+', frame_id):
        raise ValueError('Invalid sprite frame ID: ' + frame_id)
    manifest = manifest or load_manifest(root)
    source = source or ClassicSource(root)
    frame = next((f for f in manifest['frames'] if f['id'] == frame_id), None)
    if not frame or frame['recipe'] not in ai_recipes():
        raise ValueError(f'{frame_id}: select an existing AI-upscaled sprite; originals are excluded')
    if frame_id.startswith(EXCLUDED_PREFIXES):
        raise ValueError(f'{frame_id}: walls and building sites are excluded (connected seams)')
    roles = {}
    for layer in frame['layers']:
        if layer['role'] not in ('base', 'team'):
            continue
        suffix = 'r.png' if layer['role'] == 'team' else '.png'
        if layer['role'] in roles or layer['file'] != frame_id + suffix:
            raise ValueError('Invalid or duplicate sprite layer: ' + layer['file'])
        roles[layer['role']] = layer
        from io import BytesIO
        from PIL import Image
        data = source.read(layer['file'])
        if data is None:
            raise ValueError('Missing classic layer: ' + layer['file'])
        with Image.open(BytesIO(data)) as image:
            if image.size != (layer['logical_width'], layer['logical_height']):
                raise ValueError('Classic source dimensions differ: ' + layer['file'])
    expected = {role for role, suffix in (('base', '.png'), ('team', 'r.png'))
                if source.read(frame_id + suffix) is not None}
    if 'base' not in roles or set(roles) != expected:
        raise ValueError('Missing classic base/team layers: ' + frame_id)
    return frame


def staging_output(root, output):
    """Accept a fresh external directory or an ignored repository artifact path."""
    root, output = root.resolve(), Path(output).resolve()
    if root.is_relative_to(output) or (
        output.is_relative_to(root) and not output.is_relative_to(root / 'artifacts')
    ) or output == root / 'artifacts':
        raise ValueError('Use a fresh staging directory outside the repository or under artifacts/')
    if output.exists():
        raise ValueError('Staging output already exists; choose a fresh directory')
    return output


# ---------------------------------------------------------------- image helpers (numpy)
def load_rgba(path):
    import numpy as np
    from PIL import Image
    with Image.open(path) as image:
        return np.asarray(image.convert('RGBA')).copy()


def fill_invisible_rgb(rgba):
    """RGB with alpha-0 pixels replaced by the nearest visible colour."""
    import numpy as np
    from scipy.ndimage import distance_transform_edt
    rgb = rgba[..., :3].copy()
    empty = rgba[..., 3] <= 0
    if empty.any() and not empty.all():
        idx = distance_transform_edt(empty, return_distances=False, return_indices=True)
        rgb[empty] = rgb[idx[0][empty], idx[1][empty]]
    return rgb


def rgb_to_hsv(rgb):
    import numpy as np
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx = rgb.max(-1); mn = rgb.min(-1); d = mx - mn
    h = np.zeros_like(mx)
    nz = d > 0
    rm = nz & (r == mx); gm = nz & (g == mx) & ~rm; bm = nz & ~rm & ~gm
    dd = np.where(nz, d, 1)
    h[rm] = ((g - b) / dd)[rm]
    h[gm] = 2 + ((b - r) / dd)[gm]
    h[bm] = 4 + ((r - g) / dd)[bm]
    h = h * 60; h[h < 0] += 360
    s = np.where(mx > 0, d / np.where(mx > 0, mx, 1), 0)
    return h, s, mx


def hsv_to_rgb(h, s, v):
    import numpy as np
    hh = h / 60.0; i = np.floor(hh); f = hh - i
    p = v * (1 - s); q = v * (1 - s * f); t = v * (1 - s * (1 - f))
    i = i.astype(int) % 6
    out = np.stack([v, v, v], -1)
    for k, (a, b, c) in enumerate([(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)]):
        m = (i == k) & (s > 0)
        out[m] = np.stack([a, b, c], -1)[m]
    return out


TEAM_BASE_HUE = 150.0  # hue of RGB(51,255,153); the game shifts team hue by teamHue - 150


def team_shift(team, team_hue):
    """Recolour a team layer like Sprite::applyTeamHueShift."""
    import numpy as np
    rgb = team[..., :3].astype(np.float32) / 255.0
    h, s, v = rgb_to_hsv(rgb)
    shift = team_hue - TEAM_BASE_HUE
    h = (h + shift) % 360.0
    out = np.floor(255.0 * hsv_to_rgb(h, s, v) + 1e-4).clip(0, 255).astype(np.uint8)
    return np.concatenate([out, team[..., 3:4]], -1)


def alpha_over(dst, src):
    import numpy as np
    d = dst.astype(np.float32) / (255.0 if dst.dtype == np.uint8 else 1.0)
    s = src.astype(np.float32) / (255.0 if src.dtype == np.uint8 else 1.0)
    sa, da = s[..., 3:4], d[..., 3:4]
    oa = sa + da * (1 - sa)
    rgb = (s[..., :3] * sa + d[..., :3] * da * (1 - sa)) / np.maximum(oa, 1e-8)
    return np.concatenate([rgb, oa], -1)


def composite(base, team=None, team_hue=TEAM_BASE_HUE, background=None):
    """Base, then the hue-shifted team layer over it (game order); uint8 RGBA."""
    import numpy as np
    out = base
    if team is not None:
        out = alpha_over(out, team_shift(team, team_hue))
    if background is not None:
        bg = np.empty(base.shape[:2] + (4,), np.float32)
        bg[..., :3] = np.asarray(background, np.float32) / 255.0; bg[..., 3] = 1
        out = alpha_over(bg, out)
    return out if out.dtype == np.uint8 else np.rint(out * 255).clip(0, 255).astype(np.uint8)


def area_downscale(rgba, factor=SCALE):
    """Premultiplied box downscale; uint8 in, float32 0..1 RGBA out."""
    import numpy as np
    a = rgba.astype(np.float32) / 255.0
    h, w = a.shape[0] // factor, a.shape[1] // factor
    a = a[:h * factor, :w * factor].reshape(h, factor, w, factor, 4)
    alpha = a[..., 3].mean((1, 3))
    rgb = (a[..., :3] * a[..., 3:4]).mean((1, 3)) / np.maximum(alpha[..., None], 1e-8)
    return np.concatenate([rgb, alpha[..., None]], -1)


def up_float(arr, wh, method='bicubic'):
    import numpy as np
    from PIL import Image
    m = dict(bilinear=Image.BILINEAR, bicubic=Image.BICUBIC)[method]
    return np.asarray(Image.fromarray(arr.astype(np.float32), 'F').resize(wh, m))


def smoothstep(x, a, b):
    import numpy as np
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def gauss(x, sigma):
    import cv2
    import numpy as np
    return cv2.GaussianBlur(x.astype(np.float32), (0, 0), sigma)


def area_resize(x, wh):
    import cv2
    import numpy as np
    return cv2.resize(x.astype(np.float32), wh, interpolation=cv2.INTER_AREA)


def lum(rgb):
    return rgb[..., 0] * .299 + rgb[..., 1] * .587 + rgb[..., 2] * .114


# ---------------------------------------------------------------- inference
class SpandrelAdapter:
    """Loads the recipe's safetensors with spandrel and runs one tile at a time.

    The adapter interface (also implemented by test fakes) is: `use(name)`,
    attributes `scale`, `minimum`, `multiple_of`, `device`, and
    `forward(patch)` taking float32 HxWx3 in 0..1 and returning the clamped
    float32 (H*scale)x(W*scale)x3 output.
    """

    def __init__(self, models_dir, device='auto', threads=None):
        import torch
        self.torch = torch
        torch.set_num_threads(threads or cpu_count())
        if device == 'auto':
            device = 'mps' if torch.backends.mps.is_available() else 'cpu'
        self.device = torch.device(device)
        self.models_dir = Path(models_dir)
        self.name = self.desc = None

    def use(self, name):
        if name == self.name:
            return
        from spandrel import ModelLoader
        path = self.models_dir / MODELS[name]['file']
        if not path.is_file():
            raise ValueError(f'Missing model weights: {path} (download from {MODELS[name]["url"]})')
        if sha(path) != MODELS[name]['sha256']:
            raise ValueError(f'Model file hash differs from the recipe: {path}')
        self.desc = None  # keep at most one model resident
        self.desc = ModelLoader(device=self.device).load_from_file(str(path))
        self.desc.model.eval()
        self.name = name
        self.scale = self.desc.scale
        self.minimum = getattr(self.desc.size_requirements, 'minimum', 0) or 0
        self.multiple_of = getattr(self.desc.size_requirements, 'multiple_of', 1) or 1

    def forward(self, patch):
        torch = self.torch
        t = torch.from_numpy(patch).permute(2, 0, 1)[None].to(self.device)
        with torch.no_grad():
            y = self.desc.model(t)
        out = y[0].permute(1, 2, 0).clamp(0, 1).float().cpu().numpy()
        del t, y
        self.release()  # return MPS buffers between tiles: several streams share 24 GB
        return out

    def release(self):
        if self.device.type == 'mps':
            self.torch.mps.empty_cache()


def run_model(rgb, adapter):
    """Tiled inference with linear-ramp overlap blending; float32 RGB 0..1 at model scale."""
    import numpy as np
    s = adapter.scale
    x = rgb.astype(np.float32) / (255.0 if rgb.dtype == np.uint8 else 1.0)
    H, W = x.shape[:2]
    minsz = max(16, adapter.minimum)
    mult = adapter.multiple_of
    out = np.zeros((H * s, W * s, 3), np.float32)
    wsum = np.zeros((H * s, W * s, 1), np.float32)
    step = max(TILE - 2 * OVERLAP, 16)
    ys = list(range(0, max(H - TILE, 0) + 1, step)) or [0]
    xs = list(range(0, max(W - TILE, 0) + 1, step)) or [0]
    if ys[-1] + TILE < H:
        ys.append(H - TILE)
    if xs[-1] + TILE < W:
        xs.append(W - TILE)
    for y0 in ys:
        for x0 in xs:
            y1, x1 = min(y0 + TILE, H), min(x0 + TILE, W)
            patch = x[y0:y1, x0:x1]
            ph, pw = patch.shape[:2]
            th = max(minsz, -(-ph // mult) * mult); tw = max(minsz, -(-pw // mult) * mult)
            padded = np.pad(patch, ((0, th - ph), (0, tw - pw), (0, 0)),
                            mode='reflect' if min(ph, pw) > 1 else 'edge')
            y = adapter.forward(np.ascontiguousarray(padded))[:ph * s, :pw * s]
            wy = np.ones(ph * s, np.float32); wx = np.ones(pw * s, np.float32)
            r = OVERLAP * s
            if y0 > 0: wy[:r] = np.linspace(0, 1, r + 2)[1:-1]
            if y1 < H: wy[-r:] = np.linspace(1, 0, r + 2)[1:-1]
            if x0 > 0: wx[:r] = np.linspace(0, 1, r + 2)[1:-1]
            if x1 < W: wx[-r:] = np.linspace(1, 0, r + 2)[1:-1]
            wgt = (wy[:, None] * wx[None, :])[..., None]
            out[y0 * s:y1 * s, x0 * s:x1 * s] += y * wgt
            wsum[y0 * s:y1 * s, x0 * s:x1 * s] += wgt
    adapter.release()
    return out / np.maximum(wsum, 1e-8)


def run_chain(rgb, adapter):
    """Model 4x, area to 2x, model again (8x), area to 4x."""
    import numpy as np
    H, W = rgb.shape[:2]
    u = run_model(rgb, adapter)
    d = np.clip(area_resize(u, (W * 2, H * 2)), 0, 1)
    return area_resize(run_model(d, adapter), (W * 4, H * 4))


def run_tta4(rgb, adapter):
    import numpy as np
    acc = 0
    for k, flip in TTA4:
        x = np.rot90(rgb, k)
        if flip:
            x = x[:, ::-1]
        y = run_model(np.ascontiguousarray(x), adapter)
        if flip:
            y = y[:, ::-1]
        acc = acc + np.rot90(y, -k)
    return acc / len(TTA4)


def check_model_scale(adapter):
    if adapter.scale != SCALE:
        raise ValueError(f'{adapter.name}: model scale {adapter.scale} is not {SCALE}')


def team_on_base_canvas(base, team):
    """A team layer smaller than its base (defencetower0b0: 65x56 on 68x59) shares the
    base's top-left origin; pad it transparently to the base canvas for the joint pipeline."""
    import numpy as np
    if team is None or team.shape[:2] == base.shape[:2]:
        return team
    th, tw = team.shape[:2]
    h, w = base.shape[:2]
    if th > h or tw > w:
        raise ValueError(f'Team layer {tw}x{th} is larger than its base {w}x{h}')
    return np.pad(team, ((0, h - th), (0, w - tw), (0, 0)))


def infer_frame(base, team, adapter, models=VARIANTS['usv2-bhi']['models'], alpha_model=ALPHA_MODEL):
    """All model outputs the assembly needs, as {'<model>/<mode>/<kind>': float16 array at 4x}."""
    import numpy as np
    team = team_on_base_canvas(base, team)
    layers = {'base': base}
    if team is not None:
        layers['team'] = team
        layers['comp'] = composite(base, team)
    h, w = base.shape[:2]
    crop = (slice(PAD * SCALE, (PAD + h) * SCALE), slice(PAD * SCALE, (PAD + w) * SCALE))
    out = {}
    for model in dict.fromkeys(models + (alpha_model,)):
        adapter.use(model)
        check_model_scale(adapter)
        for kind, image in (layers.items() if model in models else ()):
            rgb = np.pad(fill_invisible_rgb(image), ((PAD, PAD), (PAD, PAD), (0, 0)), mode='edge')
            for mode, run in (('chain', run_chain), ('tta4', run_tta4)):
                up = run(rgb, adapter)
                if up.shape[:2] != (rgb.shape[0] * SCALE, rgb.shape[1] * SCALE):
                    raise ValueError(f'{model}: inference dimensions {up.shape[:2]} differ')
                out[f'{model}/{mode}/{kind}'] = np.clip(up[crop], 0, 1).astype(np.float16)
        if model == alpha_model:
            for kind, image in (('abase', base), ('ateam', team)):
                if image is None:
                    continue
                a = np.pad(image[..., 3], PAD)
                u = run_chain(np.repeat(a[..., None], 3, -1), adapter).mean(-1)
                out[f'{model}/chain/{kind}'] = np.clip(u[crop], 0, 1).astype(np.float16)
    return out


# ---------------------------------------------------------------- assembly (CPU, deterministic)
def bicubic_classic(native):
    import numpy as np
    h, w = native.shape[:2]
    x = fill_invisible_rgb(native).astype(np.float32) / 255
    return np.stack([up_float(x[..., c], (w * 4, h * 4), 'bicubic') for c in range(3)], -1).clip(0, 1)


def model_alpha(native_a, model_a):
    """Model alpha on hard native steps (3x3 range 60..120), classic bilinear elsewhere."""
    import numpy as np
    from scipy.ndimage import maximum_filter, minimum_filter
    h, w = native_a.shape; wh = (w * 4, h * 4)
    a = native_a.astype(np.float32)
    am = model_a.astype(np.float32) * 255
    a_bl = up_float(a, wh, 'bilinear')
    r = up_float(maximum_filter(a, 3) - minimum_filter(a, 3), wh, 'bilinear')
    wgt = np.clip((r - 60) / 60, 0, 1)
    am = np.where(up_float(maximum_filter(a, 5), wh, 'bilinear') < 1, 0, am)
    return np.clip(wgt * am + (1 - wgt) * a_bl, 0, 255)


def weighted_mean(arrays, weights=None):
    """Mean of model outputs; equal (or no) weights give exactly the plain mean."""
    if weights is None or len(set(weights)) == 1:
        return sum(arrays) / len(arrays)
    return sum(a * (w / sum(weights)) for a, w in zip(arrays, weights))


def mix_sources(sharp, smooth, agree, flat_w=None, weights=None):
    import numpy as np
    S = weighted_mean(sharp, weights)
    T = weighted_mean(smooth, weights)
    if len(sharp) >= 2:
        # disagreement = largest pairwise smoothed luminance difference (two models: the bench gate)
        lums = [lum(x) for x in sharp]
        d = None
        for i in range(len(lums)):
            for j in range(i + 1, len(lums)):
                dij = gauss(np.abs(lums[i] - lums[j]), 1.5)
                d = dij if d is None else np.maximum(d, dij)
        w = smoothstep(d, agree[0], agree[1]) * agree[2]
    else:  # one model: nothing to disagree with (bench assembler semantics); flat fallback only
        w = np.zeros(S.shape[:2], np.float32)
    if flat_w is not None:
        w = np.maximum(w, flat_w)
    w = w[..., None]
    return S * (1 - w) + T * w


def flat_weight(native, lo, hi, amount):
    import cv2
    import numpy as np
    L = lum(bicubic_classic(native))
    gx = cv2.Sobel(L, cv2.CV_32F, 1, 0, ksize=3); gy = cv2.Sobel(L, cv2.CV_32F, 0, 1, ksize=3)
    return (1 - smoothstep(gauss(np.hypot(gx, gy), 3.0), lo, hi)) * amount


def guided(I, p, r, eps):
    """Guided filter of RGB p by grey guide I."""
    import cv2
    import numpy as np
    box = lambda x: cv2.boxFilter(x, -1, (2 * r + 1, 2 * r + 1))
    mI = box(I); vI = box(I * I) - mI * mI
    out = []
    for c in range(3):
        pc = p[..., c]; mp = box(pc); cov = box(I * pc) - mI * mp
        a = cov / (vI + eps); b = mp - a * mI
        out.append(box(a) * I + box(b))
    return np.clip(np.stack(out, -1), 0, 1)


def lab_l_sharpen(rgb, lusm, clarity):
    """Thresholded luminance-only unsharp mask plus midtone clarity, in Lab L."""
    import cv2
    import numpy as np
    lab = cv2.cvtColor(rgb.astype(np.float32), cv2.COLOR_RGB2LAB)
    L = lab[..., 0]
    amount, sigma, threshold, under = lusm
    d = L - gauss(L, sigma)
    d = d * smoothstep(np.abs(d), threshold * 0.5, threshold * 1.5)
    d = np.where(d < 0, d * under, d)
    total = 0 + amount * d
    amount, sigma = clarity
    mid = 1 - ((L - 50) / 50) ** 2
    total = total + amount * (L - gauss(L, sigma)) * np.clip(mid, 0, 1)
    lab[..., 0] = np.clip(L + total, 0, 100)
    return np.clip(cv2.cvtColor(lab, cv2.COLOR_LAB2RGB), 0, 1)


def backproject(rgb, alpha8, native, iterations, strength):
    """Push the 4x area downscale toward the classic (premultiplied residual, bicubic up)."""
    import numpy as np
    h, w = native.shape[:2]
    na = native[..., 3:4].astype(np.float32) / 255
    nrgb = fill_invisible_rgb(native).astype(np.float32) / 255
    for _ in range(iterations):
        rgba = np.concatenate([np.clip(rgb, 0, 1) * 255, alpha8[..., None]], -1).astype(np.uint8)
        res = (nrgb - area_downscale(rgba)[..., :3]) * (na > 0.02)
        rgb = rgb + strength * np.stack([up_float(res[..., c], (w * 4, h * 4), 'bicubic') for c in range(3)], -1)
    return np.clip(rgb, 0, 1)


def rim_clamp(rgb, native, band, f):
    """Inside `band`, floor Lab L at f x the native 3x3 minimum L (bilinear 4x)."""
    import cv2
    import numpy as np
    from scipy.ndimage import minimum_filter
    h, w = native.shape[:2]
    cl = fill_invisible_rgb(native).astype(np.float32) / 255
    lo = up_float(minimum_filter(cv2.cvtColor(cl, cv2.COLOR_RGB2LAB)[..., 0], 3), (w * 4, h * 4), 'bilinear') * f
    lab = cv2.cvtColor(rgb.astype(np.float32), cv2.COLOR_RGB2LAB)
    lab[..., 0] = np.where(band, np.maximum(lab[..., 0], lo), lab[..., 0])
    return np.clip(cv2.cvtColor(lab, cv2.COLOR_LAB2RGB), 0, 1)


def boundary_band(a8, px, threshold):
    import numpy as np
    from scipy.ndimage import distance_transform_edt
    m = a8 >= threshold
    return np.where(m, distance_transform_edt(m), distance_transform_edt(~m)) <= px


def assemble(base, team, inference, recipe=RECIPE):
    """Classic layers + inference outputs -> (base RGBA uint8, team RGBA uint8 or None) at 4x."""
    import numpy as np
    from scipy.ndimage import distance_transform_edt, maximum_filter
    if inference.get('_cache_dtype', recipe['cache_dtype']) != recipe['cache_dtype']:
        raise ValueError('Inference cache precision differs from the recipe')
    get = lambda key, kind: inference[f'{key}/{kind}'].astype(np.float32)
    team_shape = team.shape[:2] if team is not None else None
    team = team_on_base_canvas(base, team)
    h, w = base.shape[:2]
    ab = model_alpha(base[..., 3], get(recipe['alpha'], 'abase'))
    at = model_alpha(team[..., 3], get(recipe['alpha'], 'ateam')) if team is not None else None
    fw = flat_weight(base, *recipe['flat'])

    def src(kind):
        return mix_sources([get(k, kind) for k in recipe['sharp']], [get(k, kind) for k in recipe['smooth']],
                           recipe['agree'], fw if kind == 'base' else None, recipe.get('weights'))

    lusm, clarity = recipe['lusm'], recipe['clarity']
    iterations, strength = recipe['backproject']
    rim_px, rim_f = recipe['rim_px'], recipe['rim_clamp']
    # ---- base
    brgb = src('base')
    if team is not None:
        r, eps, lo, hi = recipe['base_team_guided']
        m = smoothstep(gauss(up_float(team[..., 3].astype(np.float32), (w * 4, h * 4), 'bilinear') / 255, 2.0),
                       lo, hi)
        gb = guided(lum(src('comp')).astype(np.float32), brgb.astype(np.float32), int(r), eps)
        brgb = brgb * (1 - m[..., None]) + gb * m[..., None]
    brgb_pre = brgb  # the team layer is unmixed against this regularised base
    brgb = lab_l_sharpen(brgb, lusm, clarity)
    brgb = backproject(brgb, np.rint(ab), base, iterations, strength)
    band = boundary_band(np.rint(ab), rim_px, 128)
    if at is not None:
        band = band | boundary_band(np.rint(at), rim_px, 60)
    brgb = rim_clamp(brgb, base, band, rim_f)
    if at is not None:
        # Just outside the team shape the base must look like the classic base outside the
        # team: no dark ring from the under-team colour leaking past the crisp team edge.
        nb = base.copy(); nb[..., 3] = np.where(team[..., 3] > 40, 0, nb[..., 3])
        if (nb[..., 3] > 0).any():
            dist = distance_transform_edt(~(np.rint(at) >= 64))
            brgb = rim_clamp(brgb, nb, (dist > 0) & (dist <= 4) & (ab > 128), recipe['outside_clamp'])
    out_b = np.concatenate([np.rint(brgb * 255), np.rint(ab)[..., None]], -1).clip(0, 255).astype(np.uint8)
    if team is None:
        return out_b, None
    # ---- team: joint unmix from the composite, hue locked to the classic team layer
    tsep = src('team'); comp = src('comp')
    atf = at[..., None] / 255; abf = ab[..., None] / 255
    oa = atf + abf * (1 - atf)
    unmixed = np.clip((comp * oa - brgb_pre * abf * (1 - atf)) / np.maximum(atf, 1e-3), 0, 1)
    wgt = smoothstep(atf, .12, .35)
    trgb = wgt * unmixed + (1 - wgt) * tsep
    hue = rgb_to_hsv(bicubic_classic(team))[0]
    _, sat, val = rgb_to_hsv(trgb)
    trgb = hsv_to_rgb(hue, sat, val)
    trgb = lab_l_sharpen(trgb, recipe['team_lusm'], clarity)
    trgb = backproject(trgb, np.rint(at), team, iterations, strength)
    trgb = rim_clamp(trgb, team, boundary_band(np.rint(at), rim_px, 60), rim_f)
    # team colour on the alpha ramp = normalised convolution of the solid interior colour
    sigma = recipe['team_bleed']
    local_max = maximum_filter(at, 9)
    rel = np.where(local_max > 1, at / np.maximum(local_max, 1), 0)
    inner = (rel >= 0.9).astype(np.float32)
    num = np.stack([gauss(trgb[..., c] * inner, sigma) for c in range(3)], -1)
    den = gauss(inner, sigma)[..., None]
    wb = (1 - smoothstep(rel, 0.6, 0.95))[..., None] * (den > 0.02)
    trgb = np.clip(trgb * (1 - wb) + num / np.maximum(den, 1e-4) * wb, 0, 1)
    out_t = np.concatenate([np.rint(trgb * 255), np.rint(at)[..., None]], -1).clip(0, 255).astype(np.uint8)
    out_t = out_t[:team_shape[0] * SCALE, :team_shape[1] * SCALE]  # back to the team layer's own canvas
    return out_b, out_t


# ---------------------------------------------------------------- GPU lock
class GpuLock:
    """Exclusive mkdir lock compatible with the bench's `gpu_lock.sh` (pid/cmd/since files)."""

    def __init__(self, path):
        self.path = Path(path) if path else None

    def __enter__(self):
        if not self.path:
            return self
        announced = False
        while True:
            try:
                self.path.mkdir()
                break
            except FileExistsError:
                try:
                    pid = int((self.path / 'pid').read_text().strip())
                except (OSError, ValueError):
                    pid = None
                if pid is not None:
                    try:
                        os.kill(pid, 0)
                    except ProcessLookupError:
                        shutil.rmtree(self.path, ignore_errors=True)
                        continue
                    except PermissionError:
                        pass
                elif time.time() - self.path.stat().st_mtime > 30:
                    shutil.rmtree(self.path, ignore_errors=True)
                    continue
                if not announced:
                    print(f'gpu lock: waiting for pid {pid}', file=sys.stderr, flush=True)
                    announced = True
                time.sleep(2)
        (self.path / 'pid').write_text(f'{os.getpid()}\n')
        (self.path / 'cmd').write_text(' '.join(sys.argv) + '\n')
        (self.path / 'since').write_text(f'{int(time.time())}\n')
        return self

    def __exit__(self, *exc):
        if self.path:
            try:
                if int((self.path / 'pid').read_text()) == os.getpid():
                    shutil.rmtree(self.path, ignore_errors=True)
            except (OSError, ValueError):
                pass


# ---------------------------------------------------------------- staging run
_ADAPTER = _BIG = _MODELS = None


def is_big(base):
    """A frame whose 2x chain pass needs several tiles; DAT2 then holds ~7 GB of MPS memory."""
    return 2 * (max(base.shape[:2]) + 2 * PAD) > TILE


def _init_inference(models_dir, device, threads, big, models):
    global _ADAPTER, _BIG, _MODELS
    _ADAPTER = SpandrelAdapter(models_dir, device, threads)
    _BIG, _MODELS = big, models


def _infer_job(frame_id, base, team):
    import contextlib
    guard = _BIG if is_big(base) else contextlib.nullcontext()
    with guard:  # at most one big frame in flight across the streams (memory, not speed)
        start = time.time()
        result = infer_frame(base, team, _ADAPTER, *_MODELS)
    return frame_id, result, time.time() - start, str(_ADAPTER.device)


def _init_assembly(threads):
    import cv2
    cv2.setNumThreads(threads)


def _assemble_job(frame_id, base, team, inference, recipe):
    start = time.time()
    out_b, out_t = assemble(base, team, inference, recipe)
    return frame_id, out_b, out_t, time.time() - start


def package_versions():
    import importlib.metadata as md
    versions = dict(python=platform.python_version(), platform=platform.platform())
    for name in ('torch', 'spandrel', 'safetensors', 'numpy', 'scipy', 'opencv-python-headless', 'pillow'):
        try:
            versions[name] = md.version(name)
        except md.PackageNotFoundError:
            versions[name] = None
    return versions


def _jsonable(value):
    return list(value) if isinstance(value, tuple) else value


def model_records(names, models_dir=None):
    out = {}
    for name in names:
        info = MODELS[name]
        record = dict(info)
        if models_dir:
            path = Path(models_dir) / info['file']
            record['verified_sha256'] = sha(path) if path.is_file() else None
        out[name] = record
    return out


ASSEMBLY_VERSION = 'v1'  # bump whenever inference or assembly code changes pixels; part of the frame cache key


def _canonical(value):
    return json.loads(json.dumps(value, sort_keys=True))


def key_fields(record):
    """Everything that determines a staged frame's pixels, from its provenance record."""
    return _canonical(dict(
        recipe_name=record['recipe_name'], weights=record.get('weights'), recipe=record['recipe'],
        models={name: info['sha256'] for name, info in record['models'].items()},
        classic={layer['file']: layer['classic_sha256'] for layer in record['layers']},
        device=record['device'], scale=record['scale'], padding=record['padding'], tile=record['tile'],
        overlap=record['overlap'], assembly_version=record.get('assembly_version', 'v1')))


def frame_key(fields):
    return hashlib.sha256(json.dumps(fields, sort_keys=True).encode()).hexdigest()


def cached_frame(directory, frame_id, expected, provenance_dir=None):
    """Provenance record of a reusable `frame_id` in `directory` (a frame cache entry or a
    staging/salvage dir), or None. The key fields must match and every file its recorded hash."""
    directory = Path(directory)
    path = (provenance_dir or directory) / f'{frame_id}.json'
    try:
        record = json.loads(path.read_text())
        if record.get('frame') != frame_id or key_fields(record) != expected:
            return None
        roles = {layer['role'] for layer in record['layers']}
        if roles != {('team' if name.endswith('r.png') else 'base') for name in expected['classic']}:
            return None
        for layer in record['layers']:
            if sha(directory / layer['file']) != layer['candidate_sha256']:
                return None  # corrupted or edited after generation
    except (OSError, ValueError, KeyError, TypeError):
        return None
    return record


def store_frame(cache, frame_id, files, record):
    """Atomically replace the cache entry of `frame_id` with `files` ({name: bytes}) and `record`."""
    cache.mkdir(parents=True, exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix=f'.{frame_id}-', dir=cache))
    for name, data in files.items():
        (tmp / name).write_bytes(data)
    (tmp / f'{frame_id}.json').write_text(json.dumps(record, indent=2) + '\n')
    final = cache / frame_id
    if final.exists():
        old = cache / f'.{frame_id}-old-{os.getpid()}'
        final.rename(old)
        tmp.rename(final)
        shutil.rmtree(old, ignore_errors=True)
    else:
        tmp.rename(final)


def resolve_device(device):
    if device != 'auto':
        return device
    import torch
    return 'mps' if torch.backends.mps.is_available() else 'cpu'


def generate(root, frame_ids, output, models_dir, device='auto', streams=1, jobs=None,
             gpu_lock=None, adapter_factory=None, log=None, classic_rev=None, variant='bhi',
             cache=None, resume_from=()):
    """Stage candidates for `frame_ids` into a fresh `output`, all-or-nothing.

    Inference runs in `streams` worker processes (one model resident each)
    while assembly of finished frames runs in a pool of `jobs` processes.
    Every finished frame is stored at once in the persistent frame cache
    (`cache`, default `<output>.frames`), keyed by everything that determines
    its pixels; a rerun reuses matching, hash-verified entries and computes
    only the rest. `resume_from` directories (staging or salvage dirs with
    `provenance/<frame>.json`, or other caches) are imported the same way. A
    failing frame does not stop the others: the run finishes them, then
    raises without publishing. `adapter_factory` (tests) runs everything
    in-process with a fake adapter.
    """
    from concurrent.futures import ProcessPoolExecutor, FIRST_COMPLETED, wait
    from io import BytesIO
    import multiprocessing
    from PIL import Image
    log = log or (lambda *a: print(*a, flush=True))
    root = Path(root).resolve()
    spec = resolve(variant)
    recipe_name = spec['name']
    models = variant_models(variant)
    infer_args = (spec['models'], alpha_model_of(variant))
    weights = spec.get('weights')
    recipe = variant_recipe(variant)
    output = staging_output(root, output)
    cache = Path(cache) if cache else output.parent / (output.name + '.frames')
    manifest = load_manifest(root)
    source = ClassicSource(root, classic_rev)
    frames = {f: validate_frame(root, f, manifest, source) for f in frame_ids}  # validate before writing
    if not frames:
        raise ValueError('No frames selected')
    if adapter_factory is None:
        models_dir = Path(models_dir or '').resolve()
        for info in (MODELS[m] for m in models):
            path = models_dir / info['file']
            if not path.is_file() or sha(path) != info['sha256']:
                raise ValueError(f'Missing or different model file {path}; expected sha256 {info["sha256"]} '
                                 f'from {info["url"]}')
        device_key = resolve_device(device)
    else:
        device_key = 'fake'
    jobs = jobs or cpu_count()
    classic, classic_sha = {}, {}
    for frame_id in frames:
        layers = []
        for name in (f'{frame_id}.png', f'{frame_id}r.png'):
            data = source.read(name)
            if data is not None:  # hash exactly the bytes that are upscaled
                classic_sha[name] = hashlib.sha256(data).hexdigest()
                layers.append(load_rgba(BytesIO(data)))
            else:
                layers.append(None)
        classic[frame_id] = tuple(layers)
    versions = package_versions()
    tool = dict(path=str(Path(__file__).resolve().relative_to(ROOT)) if Path(__file__).resolve().is_relative_to(ROOT)
                else str(Path(__file__).resolve()), sha256=sha(Path(__file__).resolve()))
    recipe_json = {k: _jsonable(v) for k, v in recipe.items()}

    def base_record(frame_id):
        layers = [dict(file=name, role='team' if name.endswith('r.png') else 'base', classic_sha256=classic_sha[name])
                  for name in (f'{frame_id}.png', f'{frame_id}r.png') if name in classic_sha]
        return dict(frame=frame_id, variant=variant_key(variant), recipe_name=recipe_name, weights=weights,
                    alpha_model=alpha_model_of(variant), promotable=promotable(variant),
                    previous_recipe=frames[frame_id]['recipe'], classic_commit=getattr(source, 'commit', None),
                    layers=layers, recipe=recipe_json, scale=SCALE, padding=PAD, tile=TILE, overlap=OVERLAP,
                    assembly_version=ASSEMBLY_VERSION, models=model_records(models), packages=versions, tool=tool,
                    device=device_key)

    expected = {f: key_fields(base_record(f)) for f in frames}
    # ---- reuse: the frame cache first, then imported staging/salvage/cache directories
    reused = {}
    for frame_id in frames:
        record = cached_frame(cache / frame_id, frame_id, expected[frame_id])
        if record is None:
            for directory in resume_from:
                directory = Path(directory)
                for files_dir, prov_dir in ((directory / frame_id, None), (directory, directory / 'provenance')):
                    record = cached_frame(files_dir, frame_id, expected[frame_id], prov_dir)
                    if record is not None:
                        store_frame(cache, frame_id, {l['file']: (files_dir / l['file']).read_bytes()
                                                      for l in record['layers']}, record)
                        break
                if record is not None:
                    break
        if record is not None:
            reused[frame_id] = record
    todo = [f for f in frames if f not in reused]
    # Largest first, with big frames spread out so the other streams keep working on
    # small ones while one big frame holds the big-frame lock.
    order = sorted(todo, key=lambda f: -classic[f][0].shape[0] * classic[f][0].shape[1]
                   * (3 if classic[f][1] is not None else 1))
    big = [f for f in order if is_big(classic[f][0])]
    small = [f for f in order if not is_big(classic[f][0])]
    order = []
    while big or small:
        order += big[:1] + small[:max(1, streams - 1)]
        big, small = big[1:], small[max(1, streams - 1):]
    log(f'reused {len(reused)}, computing {len(todo)} (frame cache {cache})')
    output.parent.mkdir(parents=True, exist_ok=True)
    started = time.time()
    failures = {}
    done_count = [len(reused)]

    def write(frame_id, out_b, out_t, info):
        """Encode a finished frame and store it in the cache at once."""
        base, team = classic[frame_id]
        record = base_record(frame_id)
        files = {}
        for layer, native, image in zip(record['layers'], (base, team), (out_b, out_t)):
            if image is None or image.shape[:2] != (native.shape[0] * SCALE, native.shape[1] * SCALE):
                raise ValueError(f'{frame_id}: {layer["role"]} candidate has the wrong size')
            buffer = BytesIO()
            Image.fromarray(image, 'RGBA').save(buffer, 'PNG')
            files[layer['file']] = buffer.getvalue()
            layer.update(candidate_sha256=hashlib.sha256(files[layer['file']]).hexdigest(),
                         logical_size=[native.shape[1], native.shape[0]])
        record.update(info)
        record['device'] = device_key
        store_frame(cache, frame_id, files, record)
        done_count[0] += 1
        log(f'[{done_count[0]}/{len(frames)}] {frame_id} inference {info["inference_seconds"]:.1f}s '
            f'assembly {info["assembly_seconds"]:.1f}s ({time.time() - started:.0f}s elapsed)')

    def fail(frame_id, error):
        failures[frame_id] = f'{type(error).__name__}: {error}'
        log(f'FAILED {frame_id}: {failures[frame_id]}')

    device_used = device_key
    if adapter_factory is not None:
        adapter = adapter_factory()
        for frame_id in order:
            try:
                t0 = time.time()
                inference = infer_frame(*classic[frame_id], adapter, *infer_args)
                t1 = time.time()
                out_b, out_t = assemble(*classic[frame_id], inference, recipe)
                write(frame_id, out_b, out_t, dict(inference_seconds=t1 - t0, assembly_seconds=time.time() - t1))
            except Exception as error:  # noqa: BLE001 - one frame must not stop the others
                fail(frame_id, error)
    elif order:
        threads = max(1, cpu_count() // streams)
        with GpuLock(gpu_lock), \
                multiprocessing.Manager() as manager, \
                ProcessPoolExecutor(streams, initializer=_init_inference,
                                    initargs=(str(models_dir), device, threads, manager.Lock(), infer_args)) as gpu, \
                ProcessPoolExecutor(jobs, initializer=_init_assembly, initargs=(max(1, cpu_count() // jobs),)) as cpu:
            pending = {}
            for f in order:
                pending[gpu.submit(_infer_job, f, *classic[f])] = f
            infos = {}
            try:
                while pending:
                    done, _ = wait(set(pending), return_when=FIRST_COMPLETED)
                    for future in done:
                        frame_id = pending.pop(future)
                        try:
                            result = future.result()
                            if len(result) == 4 and isinstance(result[1], dict):  # inference finished
                                _, inference, seconds, device_used = result
                                infos[frame_id] = dict(inference_seconds=seconds)
                                pending[cpu.submit(_assemble_job, frame_id, *classic[frame_id], inference,
                                                   recipe)] = frame_id
                            else:
                                _, out_b, out_t, seconds = result
                                write(frame_id, out_b, out_t, dict(infos[frame_id], assembly_seconds=seconds))
                        except Exception as error:  # noqa: BLE001 - one frame must not stop the others
                            fail(frame_id, error)
            except BaseException:
                # Interrupted (Ctrl-C, SIGTERM): stop the workers instead of waiting for in-flight
                # inference. Finished frames stay in the frame cache for the next run.
                for pool in (gpu, cpu):
                    for process in list((pool._processes or {}).values()):
                        process.terminate()
                    pool.shutdown(wait=False, cancel_futures=True)
                raise
    if failures:
        raise ValueError(f'{len(failures)} frame(s) failed, nothing published (the other frames are cached in '
                         f'{cache}; rerun to compute only these): '
                         + '; '.join(f'{f}: {e}' for f, e in sorted(failures.items())))
    # ---- publish all-or-nothing from the verified cache
    with tempfile.TemporaryDirectory(prefix=output.name + '-staging-', dir=output.parent) as tmp:
        stage = Path(tmp) / 'result'
        (stage / 'provenance').mkdir(parents=True)
        for frame_id in frames:
            record = cached_frame(cache / frame_id, frame_id, expected[frame_id])
            if record is None:
                raise ValueError(f'{frame_id}: frame cache entry changed during the run')
            for layer in record['layers']:
                shutil.copyfile(cache / frame_id / layer['file'], stage / layer['file'])
            record['reused'] = frame_id in reused
            (stage / 'provenance' / f'{frame_id}.json').write_text(json.dumps(record, indent=2) + '\n')
        run = dict(variant=variant_key(variant), recipe_name=recipe_name, weights=weights,
                   alpha_model=alpha_model_of(variant), promotable=promotable(variant), frames=sorted(frames),
                   classic_source=dict(rev=classic_rev, commit=getattr(source, 'commit', None)), device=device_used,
                   streams=streams, jobs=jobs, wall_seconds=time.time() - started, reused=sorted(reused),
                   computed=sorted(todo), frame_cache=str(cache), assembly_version=ASSEMBLY_VERSION,
                   models=model_records(models, models_dir if adapter_factory is None else None),
                   packages=versions, tool=tool, recipe=recipe_json)
        (stage / 'run.json').write_text(json.dumps(run, indent=2) + '\n')
        stage.rename(output)
    return run


# ---------------------------------------------------------------- promotion (Pillow only, no inference)
def load_staging(staging, root):
    """Validate a staged run against the current repository; returns [(frame_id, provenance)]."""
    staging = Path(staging)
    run = json.loads((staging / 'run.json').read_text())
    variant = run.get('variant')
    if variant == 'weighted':
        variant = weighted_spec(run.get('weights') or {})
    if (variant_key(variant) not in VARIANTS and variant_key(variant) != 'weighted') \
            or run.get('recipe_name') != resolve(variant)['name']:
        raise ValueError(f'Unknown staging variant/recipe {run.get("variant")!r} / {run.get("recipe_name")!r}')
    if not promotable(variant):
        bad = [m for m in variant_models(variant) if not MODELS[m]['commercial']]
        raise ValueError(f'Variant {run.get("variant")!r} is reference-only: {bad} do not allow commercial use')
    out = []
    for frame_id in run['frames']:
        record = json.loads((staging / 'provenance' / f'{frame_id}.json').read_text())
        if record['frame'] != frame_id or record['recipe_name'] != run['recipe_name'] \
                or record.get('weights') != run.get('weights'):
            raise ValueError(f'{frame_id}: provenance does not match the run')
        for layer in record['layers']:
            if sha(staging / layer['file']) != layer['candidate_sha256']:
                raise ValueError(f'{layer["file"]}: staged candidate changed after generation')
            classic = root / CLASSIC / layer['file']
            if not classic.is_file() or sha(classic) != layer['classic_sha256']:
                raise ValueError(f'{layer["file"]}: the working-tree classic differs from the one staged; '
                                 'regenerate from the current classic before promoting')
        out.append((frame_id, record))
    return out


def registered_size(entry, role):
    layer = next(l for l in entry['layers'] if l['role'] == role)
    return layer['logical_width'], layer['logical_height']


def promote(staging, root=ROOT, only=None, skip=(), dry_run=False, log=print):
    """Write reviewed base AND team layers into both pack copies and update every index.

    Updates content-src/gfx/production/ai-upscaled/, content/data/highres/v1/,
    both manifest.json and frames.txt copies and package.json. Layers with
    other roles in an existing entry are kept. Everything is validated before
    the first write; with `dry_run` nothing is written.
    """
    from PIL import Image
    sys.path.insert(0, str(ROOT / 'tools/artwork'))
    import highres_pack as hp
    root = Path(root).resolve()
    staging = Path(staging).resolve()
    staged = load_staging(staging, root)
    selected = [(f, r) for f, r in staged if (only is None or f in only) and f not in skip]
    unknown = (set(only or ()) | set(skip)) - {f for f, _ in staged}
    if unknown:
        raise ValueError(f'Frames not in the staging run: {sorted(unknown)}')
    if not selected:
        raise ValueError('No frames selected for promotion')
    production, runtime = root / PRODUCTION, root / RUNTIME
    manifest = json.loads((production / 'pack-metadata/manifest.json').read_text())
    by_id = {frame['id']: frame for frame in manifest['frames']}
    rows = hp._read_rows(production / 'pack-metadata/frames.txt')
    row_index = {row[0]: i for i, row in enumerate(rows)}
    index = json.loads((production / 'package.json').read_text())
    records = {record['runtime']: record for record in index['files']}
    recipes = ai_recipes()
    from tools.artwork.package_runtime import AI
    if staged[0][1]['recipe_name'] not in AI:
        raise ValueError(f'Add {staged[0][1]["recipe_name"]!r} to package_runtime.AI and runtime_provenance.CATEGORIES '
                         'before promoting')
    tool_path = root / 'tools/artwork/ai/dat2_upscale.py'
    tool = hp.source_record(tool_path, root) if tool_path.is_file() else None
    plan = []
    for frame_id, record in selected:
        entry = by_id.get(frame_id)
        if entry is None or entry['recipe'] not in recipes:
            raise ValueError(f'{frame_id}: not a registered AI-upscaled frame')
        roles = {layer['role'] for layer in entry['layers'] if layer['role'] in ('base', 'team')}
        if roles != {layer['role'] for layer in record['layers']}:
            raise ValueError(f'{frame_id}: staged layers {sorted(l["role"] for l in record["layers"])} '
                             f'differ from the registered {sorted(roles)}')
        for layer in record['layers']:
            with Image.open(staging / layer['file']) as image:
                size = registered_size(entry, layer['role'])  # a team layer may be smaller than its base
                if image.mode != 'RGBA' or image.size != (size[0] * hp.SCALE, size[1] * hp.SCALE):
                    raise ValueError(f'{layer["file"]}: candidate is not {hp.SCALE}x RGBA')
            relative = f'ai-upscaled/{layer["file"]}'
            current = records.get(layer['file'])
            if current is not None and current['source'] != relative:
                raise ValueError(f'{layer["file"]}: indexed under {current["source"]}, not {relative}')
        plan.append((frame_id, record, entry))
    for frame_id, record, entry in plan:
        log(f'{"would promote" if dry_run else "promote"} {frame_id}: {entry["recipe"]} -> {record["recipe_name"]} '
            f'({", ".join(l["file"] for l in record["layers"])})')
    if dry_run:
        return plan
    for frame_id, record, entry in plan:
        layers = []
        for layer in record['layers']:
            data = (staging / layer['file']).read_bytes()  # byte-for-byte: the reviewed file
            (production / 'ai-upscaled' / layer['file']).write_bytes(data)
            (runtime / layer['file']).write_bytes(data)
            digest = hashlib.sha256(data).hexdigest()
            layers.append(dict(file=layer['file'], role=layer['role'], sha256=digest, source_sha256=digest,
                               logical_width=registered_size(entry, layer['role'])[0],
                               logical_height=registered_size(entry, layer['role'])[1],
                               original_sha256=layer['classic_sha256']))
            rec = records.get(layer['file'])
            if rec is None:
                rec = dict(runtime=layer['file'], source=f'ai-upscaled/{layer["file"]}')
                index['files'].append(rec)
                records[layer['file']] = rec
            rec['sha256'] = digest
        entry['layers'] = layers + [l for l in entry['layers'] if l['role'] not in ('base', 'team')]
        entry['recipe'] = record['recipe_name']
        entry['sources'] = [tool] if tool else []
        entry['models'] = [dict(name=name, file=info['file'], sha256=info['sha256'], url=info['url'],
                                licence=info['licence'], licence_url=info['licence_url'], author=info['author'])
                           for name, info in record['models'].items()]
        if record.get('weights'):
            entry['weights'] = record['weights']  # colour blend of the weighted ensemble
        else:
            entry.pop('weights', None)
        team = next((l['file'] for l in layers if l['role'] == 'team'), '-')
        row = [frame_id, str(entry['width']), str(entry['height']), str(hp.SCALE), f'{frame_id}.png', team]
        if frame_id in row_index:
            rows[row_index[frame_id]] = row
        else:
            rows.append(row)
    hp._save_metadata(manifest, rows, index, root)
    log(f'Promoted {len(plan)} frames. Next: python3 tools/artwork/runtime_provenance.py; '
        'python3 tools/artwork/package_runtime.py --check; python3 tools/artwork/validate_runtime.py')
    return plan


# ---------------------------------------------------------------- CLI
def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--frame', action='append', help='frame id(s), comma-separated or repeated')
    mode.add_argument('--all-buildings', action='store_true', help='every AI-upscaled building frame')
    mode.add_argument('--list-buildings', action='store_true', help='print the --all-buildings frame set')
    mode.add_argument('--promote', type=Path, metavar='STAGING', help='promote a reviewed staging run')
    parser.add_argument('--output', type=Path, help='fresh staging directory')
    parser.add_argument('--models', type=Path, help='directory holding the recipe safetensors')
    parser.add_argument('--device', default='auto', choices=('auto', 'mps', 'cpu'),
                        help='mps is bit-reproducible on one machine; cpu differs by <=1 float16 ulp')
    parser.add_argument('--streams', type=int, default=1, help='parallel inference processes')
    parser.add_argument('--jobs', type=int, default=None, help='assembly processes (default: all CPUs)')
    parser.add_argument('--gpu-lock', type=Path, default=os.environ.get('GLOB2_GPU_LOCK'),
                        help='mkdir lock shared with other GPU jobs (e.g. the bench harness/.gpu.lock)')
    parser.add_argument('--variant', choices=sorted(VARIANTS),
                        help='model set (required for inference); usv2-bhi is the non-commercial reference')
    parser.add_argument('--weights', help='weighted ensemble instead of --variant, e.g. bhi=0.5,nomos=0.3,atd=0.2 '
                        f'(sum 1; recipe {WEIGHTED_NAME!r})')
    parser.add_argument('--cache', type=Path, help='persistent frame cache (default: <output>.frames)')
    parser.add_argument('--resume-from', type=Path, action='append', default=[],
                        help='import matching finished frames from a staging, salvage or cache dir (repeatable)')
    parser.add_argument('--classic-rev', help='read the classic layers from this Git revision (e.g. HEAD) '
                        'instead of the working tree')
    parser.add_argument('--only', help='promotion: comma-separated frames to promote')
    parser.add_argument('--skip', help='promotion: comma-separated frames to leave out')
    parser.add_argument('--dry-run', action='store_true', help='promotion: validate and print the plan only')
    args = parser.parse_args(argv)
    import signal
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))  # unwind: drop partial staging, free the lock
    try:
        if args.list_buildings:
            print('\n'.join(all_building_frames(ROOT)))
        elif args.promote:
            promote(args.promote, ROOT, only=set(args.only.split(',')) if args.only else None,
                    skip=set(args.skip.split(',')) if args.skip else set(), dry_run=args.dry_run)
        else:
            if not args.output or not args.models or bool(args.variant) == bool(args.weights):
                parser.error('--output, --models and one of --variant or --weights are required for inference')
            frames = (all_building_frames(ROOT) if args.all_buildings
                      else [f for value in args.frame for f in value.split(',') if f])
            run = generate(ROOT, frames, args.output, args.models, args.device, args.streams, args.jobs,
                           args.gpu_lock, classic_rev=args.classic_rev,
                           variant=weighted_spec(args.weights) if args.weights else args.variant,
                           cache=args.cache, resume_from=args.resume_from)
            print(f'Staged {len(run["frames"])} frames for review in {args.output} '
                  f'({run["wall_seconds"]:.0f}s on {run["device"]})')
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f'DAT2 upscale failed: {error}\n')


if __name__ == '__main__':
    main()
