#!/usr/bin/env python3
"""Resize verified native GIMP wheat exports into the engine's existing canvases."""
import hashlib
import json
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
NATIVE=ROOT/'datasrc/gfx/derived/wheat-native'
OUTPUT=ROOT/'datasrc/gfx/derived/wheat-v1'
# Final ripe color states 14/19 are not in these sources; retain their fallback.
SOURCE_NAMES={10:'wheat_1_1',11:'wheat_2_1',12:'wheat_3_1',13:'wheat_4_1',
              15:'wheat_1_2',16:'wheat_2_2',17:'wheat_3_2',18:'wheat_4_2'}
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    OUTPUT.mkdir(parents=True,exist_ok=True)
    sources={Path(row['file']).stem:row for row in json.loads((NATIVE/'manifest.json').read_text())['images']}
    frames=[]
    for index,name in SOURCE_NAMES.items():
        src=sources[name];native=NATIVE/src['file'];original=ROOT/src['source']
        assert sha(native)==src['sha256'] and sha(original)==src['source_sha256']
        image=Image.open(native).convert('RGBA')
        assert list(image.size)==src['native_size']
        filename='ressource%d.png'%index
        size=Image.open(ROOT/'data/gfx'/filename).size
        image.resize((size[0]*4,size[1]*4),Image.Resampling.LANCZOS).save(OUTPUT/filename)
        frames.append(dict(id=Path(filename).stem,width=size[0],height=size[1],scale=4,
            recipe='recovered original wheat: GIMP visible layers and native alpha; premultiplied Lanczos resize',
            sources=[dict(path=src['source'],sha256=src['source_sha256'],native_size=src['native_size'],layers=src['layers']),
                     dict(path=str(native.relative_to(ROOT)),sha256=src['sha256'],native_size=src['native_size'])],
            layers=[dict(file=filename,role='base',sha256=sha(OUTPUT/filename))]))
    (OUTPUT/'manifest.json').write_text(json.dumps(dict(version=1,frames=frames),indent=2)+'\n')
    print('Exported 8 wheat frames from 8 original GIMP sources; ripe frames 14/19 retain fallback; no AI')

if __name__=='__main__': main()
