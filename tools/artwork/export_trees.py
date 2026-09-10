#!/usr/bin/env python3
"""Resize verified native GIMP tree exports into the engine's existing canvases."""
import hashlib
import json
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
NATIVE=ROOT/'datasrc/gfx/derived/tree-native'
OUTPUT=ROOT/'datasrc/gfx/derived/trees-v1'
# The two five-frame sequences share their later growth stages in classic art.
SOURCE_NAMES=['trees_1_1','trees_2_1','trees_3_1','trees_4_1','trees_4_1',
              'trees_1_2','trees_2_1','trees_3_1','trees_4_1','trees_4_1']
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    OUTPUT.mkdir(parents=True,exist_ok=True)
    sources={Path(row['file']).stem:row for row in json.loads((NATIVE/'manifest.json').read_text())['images']}
    frames=[]
    for index,name in enumerate(SOURCE_NAMES):
        src=sources[name];native=NATIVE/src['file'];original=ROOT/src['source']
        assert sha(native)==src['sha256'] and sha(original)==src['source_sha256']
        image=Image.open(native).convert('RGBA')
        assert list(image.size)==src['native_size']
        filename='ressource%d.png'%index
        size=Image.open(ROOT/'data/gfx'/filename).size
        image.resize((size[0]*4,size[1]*4),Image.Resampling.LANCZOS).save(OUTPUT/filename)
        frames.append(dict(id=Path(filename).stem,width=size[0],height=size[1],scale=4,
            recipe='recovered original tree: GIMP visible layers and native alpha; premultiplied Lanczos resize',
            sources=[dict(path=src['source'],sha256=src['source_sha256'],native_size=src['native_size'],layers=src['layers']),
                     dict(path=str(native.relative_to(ROOT)),sha256=src['sha256'],native_size=src['native_size'])],
            layers=[dict(file=filename,role='base',sha256=sha(OUTPUT/filename))]))
    (OUTPUT/'manifest.json').write_text(json.dumps(dict(version=1,frames=frames),indent=2)+'\n')
    print('Exported all 10 tree frames from 5 original GIMP sources; no AI')

if __name__=='__main__': main()
