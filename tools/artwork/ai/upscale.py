#!/usr/bin/env python3
"""Generate constrained sprite candidates into explicit staging, never game assets."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt, gaussian_filter
import scipy

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.artwork.package_runtime import AI

SCALE = 4
PADDING = 16
DETAIL_STRENGTH = 0.65
MODEL = 'realesrgan-x4plus'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def filled_rgb(source):
    """Extend visible colors into transparent pixels before model inference."""
    pixels = np.asarray(source.convert('RGBA')).copy()
    empty = pixels[:, :, 3] == 0
    if empty.any() and not empty.all():
        nearest = distance_transform_edt(empty, return_distances=False, return_indices=True)
        pixels[empty, :3] = pixels[nearest[0][empty], nearest[1][empty], :3]
    return pixels[:, :, :3]


def select_frame(root, frame_id):
    if not re.fullmatch(r'[A-Za-z0-9_-]+', frame_id):
        raise ValueError('Invalid sprite frame ID')
    manifest = json.loads((root / 'data/highres/v1/manifest.json').read_text())
    frame = next((f for f in manifest['frames'] if f['id'] == frame_id), None)
    if not frame or frame['recipe'] not in AI:
        raise ValueError('Select an existing AI-upscaled sprite; originals and connected terrain are excluded')
    roles = set()
    for layer in frame['layers']:
        name, role = layer['file'], layer['role']
        suffix = 'r.png' if role == 'team' else '.png'
        if role not in {'base', 'team'} or role in roles or name != frame_id + suffix:
            raise ValueError('Invalid or duplicate sprite layer: ' + name)
        roles.add(role)
        with Image.open(root / 'data/gfx' / name) as image:
            if image.size != (layer['logical_width'], layer['logical_height']):
                raise ValueError('Classic source dimensions differ: ' + name)
    expected = {role for role, suffix in (('base', '.png'), ('team', 'r.png'))
                if (root / 'data/gfx' / (frame_id + suffix)).is_file()}
    if not roles or roles != expected:
        raise ValueError('Missing classic base/team layers: ' + frame_id)
    return frame


def staging_output(root, output):
    """Accept a fresh external directory or an ignored repository artifact path."""
    root, output = root.resolve(), output.resolve()
    if root.is_relative_to(output) or (
        output.is_relative_to(root) and not output.is_relative_to(root / 'artifacts')
    ) or output == root / 'artifacts':
        raise ValueError('Use a fresh staging directory outside the repository or under artifacts/')
    if output.exists():
        raise ValueError('Staging output already exists; choose a fresh directory')
    return output


def finish_candidate(image, raw):
    """Restore broad source color and alpha while retaining model detail."""
    expected = ((image.width + 2 * PADDING) * SCALE,
                (image.height + 2 * PADDING) * SCALE)
    if raw.size != expected:
        raise ValueError(f'Inference dimensions {raw.size} differ from expected {expected}')
    border = PADDING * SCALE
    raw = raw.convert('RGB').crop((border, border, border + image.width * SCALE,
                                  border + image.height * SCALE))
    baseline = Image.fromarray(filled_rgb(image)).resize(raw.size, Image.Resampling.BICUBIC)
    base = np.asarray(baseline, dtype=float)
    delta = np.asarray(raw, dtype=float) - base
    detail = delta - gaussian_filter(delta, (8, 8, 0), mode='nearest')
    rgb = np.clip(np.rint(base + DETAIL_STRENGTH * detail), 0, 255).astype(np.uint8)
    candidate = Image.fromarray(rgb)
    candidate.putalpha(image.getchannel('A').resize(raw.size, Image.Resampling.BILINEAR))
    return candidate


def generate(root, frame_id, output, executable=None, models=None, prepare_only=False):
    root = Path(root).resolve()
    output = staging_output(root, Path(output))
    frame = select_frame(root, frame_id)  # Validate every layer before creating output.
    inference = None
    if not prepare_only:
        if not executable or not models:
            raise ValueError('Inference requires an external executable and models directory')
        executable, models = Path(executable).resolve(), Path(models).resolve()
        if not executable.is_file() or not models.is_dir():
            raise ValueError('Inference executable or models directory does not exist')
        weights = [models / (MODEL + suffix) for suffix in ('.bin', '.param')]
        if not all(path.is_file() for path in weights):
            raise ValueError('Missing model weights or parameter file for ' + MODEL)
        inference = dict(executable_sha256=sha(executable),
                         models={path.name: sha(path) for path in weights})
    output.parent.mkdir(parents=True, exist_ok=True)
    # Publish a complete result only. Failed inference cannot leave stale candidates
    # beside a newer recipe or expose a half-finished base/team pair.
    with tempfile.TemporaryDirectory(prefix=output.name + '-staging-', dir=output.parent) as tmp:
        stage = Path(tmp) / 'result'
        for folder in ('input', 'raw', 'candidate'):
            (stage / folder).mkdir(parents=True)
        records = []
        for layer in frame['layers']:
            name = layer['file']
            source = root / 'data/gfx' / name
            with Image.open(source) as native:
                image = native.convert('RGBA')
            padded = np.pad(filled_rgb(image), ((PADDING, PADDING), (PADDING, PADDING), (0, 0)), mode='edge')
            Image.fromarray(padded).save(stage / 'input' / name)
            row = dict(file=name, role=layer['role'], source_sha256=sha(source), logical_size=list(image.size))
            if not prepare_only:
                subprocess.run([str(executable), '-i', str(stage / 'input' / name),
                                '-o', str(stage / 'raw' / name), '-m', str(models),
                                '-n', MODEL, '-s', str(SCALE), '-j', '1:1:1', '-f', 'png'], check=True)
                with Image.open(stage / 'raw' / name) as raw:
                    candidate = finish_candidate(image, raw)
                candidate.save(stage / 'candidate' / name)
                row['candidate_sha256'] = sha(stage / 'candidate' / name)
            records.append(row)
        recipe = dict(frame=frame_id, stage='prepared' if prepare_only else 'candidate',
                      model=MODEL, scale=SCALE, padding=PADDING, detail_strength=DETAIL_STRENGTH,
                      approved_recipe=frame['recipe'], layers=records)
        recipe['tool_versions'] = dict(pillow=Image.__version__, numpy=np.__version__, scipy=scipy.__version__)
        if inference is not None:
            recipe['inference'] = inference
        (stage / 'recipe.json').write_text(json.dumps(recipe, indent=2) + '\n')
        stage.rename(output)
    return recipe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frame', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--executable', type=Path)
    parser.add_argument('--models', type=Path)
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    try:
        generate(ROOT, args.frame, args.output, args.executable, args.models, args.prepare_only)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Artwork candidate failed: {error}\n')
    print('Prepared inputs' if args.prepare_only else 'Generated candidates for visual review', args.output)


if __name__ == '__main__':
    main()
