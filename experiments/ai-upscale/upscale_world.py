#!/usr/bin/env python3
"""Finish non-unit world art; keep masks/overlays deterministic and tile edges stable."""
import argparse,re,json
from pathlib import Path
import numpy as np
from PIL import Image
import super_resolution as pipeline

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--cache',type=Path,default=pipeline.CACHE);parser.add_argument('--reuse-inference',action='store_true');args=parser.parse_args()
    pipeline.CACHE=args.cache.resolve()
    pipeline.EXE=pipeline.CACHE/'runtime/realesrgan-ncnn-vulkan-v0.2.0-macos/realesrgan-ncnn-vulkan'
    pipeline.MODELS=pipeline.CACHE/'bundle/models'
    pipeline.OUT=pipeline.ROOT/'experiments/ai-upscale/world'
    gfx=pipeline.ROOT/'data/gfx'
    pipeline.NAMES=[f'terrain{i}.png' for i in range(16,272)]
    pipeline.NAMES += sorted(p.name for p in gfx.glob('*.png') if re.fullmatch(r'(water|bullet|explosion|magiceffect|particle)\d+r?\.png',p.name))
    pipeline.prepare()
    command=[str(pipeline.EXE),'-i',str(pipeline.OUT/'input'),'-o',str(pipeline.OUT/'raw'),'-m',str(pipeline.MODELS),'-n','realesrgan-x4plus','-s','4','-j','1:1:1','-f','png']
    pipeline.finish(command if args.reuse_inference else pipeline.run(),make_preview=False)
    for name in pipeline.NAMES:
        if name.startswith('water'):
            # Water repeats in both axes. Keep the source boundary intact and
            # introduce only restrained, smoothly faded model detail inside it.
            path=pipeline.OUT/'corrected'/name
            a=np.asarray(Image.open(path)).astype(float)
            b=np.asarray(Image.open(pipeline.OUT/'baseline'/name).convert('RGBA')).astype(float)
            y,x=np.indices(a.shape[:2]);edge=np.minimum.reduce([x,y,a.shape[1]-1-x,a.shape[0]-1-y])
            weight=np.clip((edge-4)/32,0,1)[...,None]
            a[...,:3]=b[...,:3]+np.clip(a[...,:3]-b[...,:3],-6,6)*weight*.2
            Image.fromarray(np.round(a).clip(0,255).astype('uint8')).save(path)
    # Fog, clouds and area markings are soft masks, not surfaces needing invented
    # detail. Resample the original channels together to preserve their meaning.
    for p in gfx.glob('*.png'):
        if re.fullmatch(r'(cloud|black|shade|area-clearing|area-forbidden|area-guard)\d+\.png',p.name):
            im=Image.open(p).convert('RGBA');im.resize((im.width*4,im.height*4),Image.Resampling.BILINEAR).save(pipeline.OUT/'corrected'/p.name)

    provenance_path=pipeline.OUT/'provenance.json'
    provenance=json.loads(provenance_path.read_text())
    provenance['postprocessing']={'water':'RGB residual limited to +/-6, mixed at 20%, zero within 4 output pixels of the edge, fading over 32 pixels','soft_masks':'original RGBA resampled bilinearly at 4x; no model inference','metrics':'metrics.json describes the constrained inference output before water postprocessing'}
    provenance_path.write_text(json.dumps(provenance,indent=2)+'\n')
