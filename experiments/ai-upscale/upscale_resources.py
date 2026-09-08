#!/usr/bin/env python3
"""Apply the selected constrained local RGB pipeline to all world resource frames."""
import argparse
from pathlib import Path
import re
import super_resolution as pipeline

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--cache',type=Path,default=pipeline.CACHE)
    args=parser.parse_args()
    pipeline.CACHE=args.cache.resolve()
    pipeline.EXE=pipeline.CACHE/'runtime/realesrgan-ncnn-vulkan-v0.2.0-macos/realesrgan-ncnn-vulkan'
    pipeline.MODELS=pipeline.CACHE/'bundle/models'
    pipeline.OUT=pipeline.ROOT/'experiments/ai-upscale/resources'
    pipeline.NAMES=sorted(p.name for p in (pipeline.ROOT/'data/gfx').glob('ressource*.png') if re.fullmatch(r'ressource\d+r?\.png',p.name))
    pipeline.prepare()
    pipeline.finish(pipeline.run(),make_preview=False)
