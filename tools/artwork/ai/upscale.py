#!/usr/bin/env python3
"""Generate constrained sprite candidates into explicit staging, never game assets."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import numpy as np
from PIL import Image
from scipy.ndimage import distance_transform_edt, gaussian_filter

ROOT = Path(__file__).resolve().parents[3]

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def filled_rgb(source):
    pixels = np.asarray(source.convert('RGBA')).copy()
    empty = pixels[:, :, 3] == 0
    if empty.any() and not empty.all():
        nearest = distance_transform_edt(empty, return_distances=False, return_indices=True)
        pixels[empty, :3] = pixels[nearest[0][empty], nearest[1][empty], :3]
    return pixels[:, :, :3]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--frame', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--executable', type=Path)
    parser.add_argument('--models', type=Path)
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    for protected in [ROOT/'data', ROOT/'datasrc', ROOT/'tools']:
        if output == protected or protected in output.parents:
            parser.error('Use a separate staging output, not production/source directories')
    manifest = json.loads((ROOT/'data/highres/v1/manifest.json').read_text())
    frame = next((f for f in manifest['frames'] if f['id'] == args.frame), None)
    if not frame or frame['recipe'] not in {'current','outline_repair','painted_repair','crystal_repair','resource constrained','world constrained'}:
        parser.error('Select an existing AI-upscaled sprite; original artwork and connected terrain are excluded')
    if not args.prepare_only and (not args.executable or not args.models):
        parser.error('Inference requires an external realesrgan-ncnn-vulkan executable and models directory')
    for folder in ['input','raw','candidate']:
        (output/folder).mkdir(parents=True, exist_ok=True)
    records = []
    for layer in frame['layers']:
        name = layer['file'];source = ROOT/'data/gfx'/name
        image = Image.open(source).convert('RGBA')
        padded = np.pad(filled_rgb(image),((16,16),(16,16),(0,0)),mode='edge')
        Image.fromarray(padded).save(output/'input'/name)
        row = dict(file=name, role=layer['role'], source_sha256=sha(source),logical_size=list(image.size))
        if not args.prepare_only:
            subprocess.run([str(args.executable.resolve()),'-i',str(output/'input'/name),'-o',str(output/'raw'/name),
                '-m',str(args.models.resolve()),'-n','realesrgan-x4plus','-s','4','-j','1:1:1','-f','png'],check=True)
            raw = Image.open(output/'raw'/name).convert('RGB')
            assert raw.size == ((image.width+32)*4,(image.height+32)*4)
            raw = raw.crop((64,64,64+image.width*4,64+image.height*4))
            baseline = Image.fromarray(filled_rgb(image)).resize(raw.size,Image.Resampling.BICUBIC)
            b=np.asarray(baseline,dtype=float);delta=np.asarray(raw,dtype=float)-b
            detail=delta-gaussian_filter(delta,(8,8,0),mode='nearest')
            rgb=np.clip(np.rint(b+.65*detail),0,255).astype(np.uint8)
            candidate=Image.fromarray(rgb)
            alpha=image.getchannel('A').resize(raw.size,Image.Resampling.BILINEAR)
            candidate.putalpha(alpha)
            candidate.save(output/'candidate'/name)
            row['candidate_sha256']=sha(output/'candidate'/name)
        records.append(row)
    (output/'recipe.json').write_text(json.dumps(dict(frame=args.frame,stage='prepared' if args.prepare_only else 'candidate',
        model='realesrgan-x4plus',scale=4,padding=16,detail_strength=.65,
        approved_recipe=frame['recipe'],layers=records),indent=2)+'\n')
    print('Prepared inputs' if args.prepare_only else 'Generated candidates for visual review',output)

if __name__ == '__main__':
    main()
