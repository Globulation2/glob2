#!/usr/bin/env python3
"""Constrained 4x Real-ESRGAN trial. Run with the experiment's cached venv.

Edits only experiments/ai-upscale/sr and .cache/ai-upscale; no game writes.
"""
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import distance_transform_edt, gaussian_filter

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'experiments/ai-upscale/sr'
CACHE = ROOT / '.cache/ai-upscale'
EXE = CACHE / 'runtime/realesrgan-ncnn-vulkan-v0.2.0-macos/realesrgan-ncnn-vulkan'
MODELS = CACHE / 'bundle/models'
SCALE = 4
NAMES = [p.name for p in sorted((ROOT/'data/gfx').glob('inn0*.png'))] + [f'terrain{i}.png' for i in range(16)]


def image(a):
    return Image.fromarray(np.clip(np.rint(a), 0, 255).astype(np.uint8))


def fill_rgb(im):
    a = np.array(im.convert('RGBA'))
    transparent = a[:, :, 3] == 0
    if transparent.any() and not transparent.all():
        indices = distance_transform_edt(transparent, return_distances=False, return_indices=True)
        a[transparent, :3] = a[indices[0][transparent], indices[1][transparent], :3]
    return Image.fromarray(a[:, :, :3])


def smooth(im):
    return im.resize((im.width*SCALE, im.height*SCALE), Image.Resampling.BICUBIC)


def source_alpha(im):
    # Palette/tRNS PNGs can carry partial alpha without an explicit A band.
    if 'A' in im.getbands() or 'transparency' in im.info:
        return im.convert('RGBA').getchannel('A')
    return None


def prepare():
    for d in ('input', 'raw', 'baseline', 'corrected', 'nearest', 'composites'):
        (OUT/d).mkdir(parents=True, exist_ok=True)
    for name in NAMES:
        src = Image.open(ROOT/'data/gfx'/name)
        rgb = fill_rgb(src)
        if name.startswith('terrain'):
            # Five repeats put the extracted center 64 source pixels from the boundary.
            rgb = Image.fromarray(np.tile(np.array(rgb), (5, 5, 1)))
        else:
            rgb = Image.fromarray(np.pad(np.array(rgb), ((16,16),(16,16),(0,0)), mode='edge'))
        rgb.save(OUT/'input'/name)
        src.resize((src.width*SCALE, src.height*SCALE), Image.Resampling.NEAREST).save(OUT/'nearest'/name)
        base = smooth(fill_rgb(src))
        alpha = source_alpha(src)
        if alpha is not None:
            base.putalpha(alpha.resize(base.size, Image.Resampling.BILINEAR))
        base.save(OUT/'baseline'/name)


def run():
    command = [str(EXE), '-i', str(OUT/'input'), '-o', str(OUT/'raw'),
               '-m', str(MODELS), '-n', 'realesrgan-x4plus', '-s', str(SCALE), '-j', '1:1:1', '-f', 'png']
    with (OUT/'inference.log').open('w') as log:
        subprocess.run(command, stdout=log, stderr=log, check=True)
    return command


def finish(command, make_preview=True):
    rows = []
    for name in NAMES:
        src = Image.open(ROOT/'data/gfx'/name)
        raw = Image.open(OUT/'raw'/name).convert('RGB')
        inp = Image.open(OUT/'input'/name)
        assert raw.size == (inp.width*SCALE, inp.height*SCALE), (name, raw.size)
        start = 64*SCALE if name.startswith('terrain') else 16*SCALE
        raw = raw.crop((start, start, start+src.width*SCALE, start+src.height*SCALE))
        base = Image.open(OUT/'baseline'/name)
        a = np.asarray(raw).astype(float)
        b = np.asarray(base.convert('RGB')).astype(float)
        # Keep source broad colors. Grass needs a strict residual cap because
        # the model invents visibly inconsistent grain on some variants.
        delta = a-b
        mode = 'wrap' if name.startswith('terrain') else 'nearest'
        detail = delta-gaussian_filter(delta, sigma=(SCALE*2, SCALE*2, 0), mode=mode)
        weight = np.ones(a.shape[:2])
        strength = 0.65
        if name.startswith('terrain'):
            strength = 0.2
            detail = np.clip(detail, -6, 6)
            yy, xx = np.indices(weight.shape)
            edge = np.minimum.reduce([yy, xx, weight.shape[0]-1-yy, weight.shape[1]-1-xx])
            # Keep the outer source-pixel-wide band identical to baseline. All
            # grass variants already share source borders; do not invent new ones.
            weight = np.clip((edge-SCALE)/(3*SCALE), 0, 1)
            weight = weight*weight*(3-2*weight)
        corrected = image(b+strength*detail*weight[:,:,None])
        if source_alpha(src) is not None:
            alpha = base.getchannel('A')
            corrected.putalpha(alpha)
            raw.putalpha(alpha)
            assert np.array_equal(np.array(corrected.getchannel('A')), np.array(alpha))
        corrected.save(OUT/'corrected'/name)
        raw.save(OUT/'composites'/('raw-'+name))
        row = dict(name=name, source_sha256=hashlib.sha256((ROOT/'data/gfx'/name).read_bytes()).hexdigest(),
                   source_size=list(src.size), output_size=list(corrected.size),
                   alpha_matches_baseline=True if source_alpha(src) is not None else None,
                   mean_rgb_shift=(np.array(corrected.convert('RGB')).mean((0,1))-b.mean((0,1))).tolist())
        if name.startswith('terrain'):
            c=np.array(corrected.convert('RGB'))
            bb=np.array(base.convert('RGB'))
            assert np.array_equal(c[:SCALE],bb[:SCALE]) and np.array_equal(c[-SCALE:],bb[-SCALE:])
            assert np.array_equal(c[:,:SCALE],bb[:,:SCALE]) and np.array_equal(c[:,-SCALE:],bb[:,-SCALE:])
            row['edge_band_matches_baseline']=True
        rows.append(row)
    seams = {}
    for kind in (('nearest','baseline','corrected') if all(f'terrain{i}.png' in NAMES for i in range(16)) else ()):
        tiles=[np.array(Image.open(OUT/kind/f'terrain{i}.png')).astype(float) for i in range(16)]
        seams[kind] = dict(horizontal_all_pairs_mean=float(np.mean([np.abs(a[:,-1]-b[:,0]).mean() for a in tiles for b in tiles])),
                           vertical_all_pairs_mean=float(np.mean([np.abs(a[-1]-b[0]).mean() for a in tiles for b in tiles])))
    if seams:
        assert seams['baseline'] == seams['corrected']
    (OUT/'metrics.json').write_text(json.dumps(dict(assets=rows, seams=seams),indent=2)+'\n')
    provenance = dict(model='realesrgan-x4plus', scale=SCALE, command=command,
                      runtime_url='https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan/releases/tag/v0.2.0',
                      models_url='https://github.com/xinntao/Real-ESRGAN/releases/tag/v0.2.5.0',
                      sha256={str(p.relative_to(CACHE)):hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in (EXE, MODELS/'realesrgan-x4plus.bin', MODELS/'realesrgan-x4plus.param')})
    (OUT/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    if make_preview:
        preview()
    print(json.dumps(dict(processed=len(rows), seams=seams),indent=2))


def preview():
    sheet=Image.new('RGB',(1152,760),(22,29,25)); draw=ImageDraw.Draw(sheet)
    kinds=['nearest','baseline','raw','corrected']
    titles=['Original / nearest','Bicubic baseline','Real-ESRGAN / raw RGB','Real-ESRGAN / constrained']
    for col,(kind,title) in enumerate(zip(kinds,titles)):
        x=col*288
        draw.text((x+12,12),title,fill='white')
        directory=OUT/('composites' if kind=='raw' else kind)
        prefix='raw-' if kind=='raw' else ''
        base=Image.open(directory/(prefix+'inn0b0.png')).convert('RGBA')
        overlay=Image.open(directory/(prefix+'inn0b0r.png')).convert('RGBA')
        complete=Image.alpha_composite(base,overlay)
        complete.save(OUT/'composites'/f'inn-{kind}.png')
        sheet.paste(complete,(x+16,40),complete)
        for i in range(16):
            tile=Image.open(directory/(prefix+f'terrain{i}.png')).convert('RGB').resize((64,64),Image.Resampling.LANCZOS if kind!='nearest' else Image.Resampling.NEAREST)
            sheet.paste(tile,(x+16+(i%4)*64,330+(i//4)*64))
        native=complete.resize((64,64),Image.Resampling.LANCZOS)
        sheet.paste(native,(x+16,630),native)
        draw.text((x+16,710),'Inn at native 64px; tiles above at 2x',fill='#aebfb3')
    sheet.save(OUT/'comparison.png')


if __name__ == '__main__':
    prepare()
    finish(run())
