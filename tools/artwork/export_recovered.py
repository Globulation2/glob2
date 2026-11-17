#!/usr/bin/env python3
"""Deterministically export recovered PNG artwork. No AI or classic geometry masks."""
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'content-src/gfx/reference-exports/buildings'
OUTPUT = ROOT / 'content-src/gfx/derived/recovered-v1'

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path): return np.asarray(Image.open(path).convert('RGBA'), dtype=float) / 255

def unmatte(rgb, alpha):
    """Undo a white rendering background in straight-alpha space."""
    color = np.clip((rgb - (1 - alpha[..., None])) / np.maximum(alpha[..., None], 1/255), 0, 1)
    color[alpha == 0] = 0
    return np.dstack((color, alpha))

def green_on_white(a, work_size):
    # Source art is green with neutral projected shadows. Work from the native
    # render, never the misregistered generated sprites or a classic-size mask.
    # Interpolate the render on its white ground to at least twice the output
    # size first, so the matte below follows smooth edges instead of the native
    # pixel staircase.
    white = a[..., :3] * a[..., 3:] + (1 - a[..., 3:])
    native = Image.fromarray(np.uint8(np.rint(white * 255)), 'RGB')
    if native.size[0] < work_size:
        native = native.resize((work_size, work_size), Image.Resampling.LANCZOS)
    rgb = np.asarray(native, dtype=float) / 255
    cover = 1 - rgb.min(axis=2)
    # Over white, a pixel's chroma and coverage scale together, so their ratio
    # is its own saturation however little of the pixel it covers. Green object
    # and neutral shadow therefore split softly along anti-aliased edges.
    saturation = (rgb.max(axis=2) - rgb.min(axis=2)) / np.maximum(cover, 1e-3)
    team_weight = np.clip((saturation - 0.10) / 0.20, 0, 1)
    team_weight[cover < 0.01] = 0
    object_mask = (team_weight > 0.5) & (cover > 0.03)
    # Fill small specular highlights, not the background seen through a frame.
    holes, count = ndimage.label(ndimage.binary_fill_holes(object_mask) & ~object_mask)
    sizes = ndimage.sum(np.ones(holes.shape), holes, range(1, count + 1))
    small = np.flatnonzero(sizes < (work_size / 32) ** 2) + 1
    object_mask |= np.isin(holes, small)
    # Keep interior highlights opaque at their rendered color; remove white
    # contamination only on edge pixels.
    interior = ndimage.binary_erosion(object_mask, iterations=max(1, work_size // 256))
    color = unmatte(rgb, cover)[..., :3]
    team = np.dstack((color, cover * team_weight))
    team[interior] = np.dstack((rgb, np.ones(cover.shape)))[interior]
    team[team[..., 3] == 0] = 0
    shadow = cover * (1 - team_weight)
    shadow[interior] = 0
    base = np.dstack((color, shadow))
    # Match the classic game's subdued shadow style, using only its color and
    # opacity, not its low-resolution silhouette. Native render defines geometry.
    base[..., :3] = np.array([65, 66, 69]) / 255
    base[..., 3] = np.minimum(base[..., 3] / 0.75, 1) * (144 / 255)
    base[base[..., 3] == 0] = 0
    return base, team

def export():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    records = []
    def save(frame, arrays, sources, method):
        classic = ROOT / 'content/data/gfx' / (frame + '.png')
        if not classic.exists(): classic = classic.with_name(frame + 'r.png')
        size = Image.open(classic).size
        layers = []
        for role, a in arrays.items():
            filename = frame + ('r' if role == 'team' else '') + '.png'
            img = Image.fromarray(np.uint8(np.rint(np.clip(a, 0, 1) * 255)), 'RGBA')
            # Pillow's RGBA resize filters in premultiplied alpha space.
            img = img.resize((size[0]*4, size[1]*4), Image.Resampling.LANCZOS)
            if role == 'base' and 'team' in arrays:
                pixels = np.array(img)
                pixels[..., 3] = np.minimum(pixels[..., 3], 144)
                # Drop invisible resampling residue so shadowless frames stay empty.
                pixels[pixels[..., 3] < 4] = 0
                pixels[..., :3] = [65, 66, 69]
                pixels[pixels[..., 3] == 0] = 0
                img = Image.fromarray(pixels, 'RGBA')
            path = OUTPUT / filename; img.save(path)
            layers.append(dict(file=filename, role=role, sha256=sha(path)))
        records.append(dict(id=frame, width=size[0], height=size[1], scale=4,
            recipe='recovered original: ' + method, layers=layers,
            sources=[dict(path=str(p.relative_to(ROOT)), sha256=sha(p), native_size=list(Image.open(p).size)) for p in sources]))

    for frame, filename in [('swarm0b0','Ruche.png'), ('swarm0c0','Morph128c.png'),
                            ('warflag0','WarFlag.png'), ('explorationflag0','ExploFlag.png'),
                            ('clearingflag0','RemoveFlag.png')]:
        source = SOURCE / ('swarm' if frame.startswith('swarm') else 'flags') / filename
        classic = ROOT / 'content/data/gfx' / (frame + 'r.png')
        base, team = green_on_white(read(source), Image.open(classic).size[0] * 8)
        save(frame, dict(base=base, team=team), [source], 'native white-matte removal; green team / neutral shadow separation')
    for frame, number in [('buildingsite1',64),('buildingsite2',96),('buildingsite3',128),
                          ('buildingsite4',192),('buildingsite5',256)]:
        color = SOURCE / 'construction' / ('Construction%dc.png' % number)
        mask = color.with_name('Construction%dm.png' % number)
        a=read(color);m=read(mask)
        assert a.shape == m.shape
        alpha=m[..., :3].mean(axis=2)*m[...,3]*a[...,3]
        save(frame, dict(base=unmatte(a[...,:3],alpha)), [color,mask], 'native matching construction matte; white-background unpremultiplication')
    (OUTPUT / 'manifest.json').write_text(json.dumps(dict(version=1, frames=records), indent=2)+'\n')
    print('Exported %d original frames to %s' % (len(records),OUTPUT))

if __name__ == '__main__': export()
