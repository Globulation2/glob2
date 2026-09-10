#!/usr/bin/env python3
"""Export all original area-marker animation frames without AI or resampling."""
import hashlib,json
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'datasrc/gfx/reference-exports/overlays'
OUTPUT=ROOT/'datasrc/gfx/derived/markers-v1'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    OUTPUT.mkdir(parents=True,exist_ok=True)
    frames=[]
    for family,prefix,offset in [('guard','g',0),('clearing','h',1),('forbidden','i',0)]:
        for index in range(8):
            # Recovered versions are authoritative; i4 was byte-identical to
            # its pre-existing reference and reused by the archive importer.
            source=SOURCE/('%s%d-archive-2026.png'%(prefix,index+offset))
            if not source.exists():
                assert family=='forbidden' and index==4
                source=SOURCE/'i4.png'
            filename='area-%s%d.png'%(family,index)
            image=Image.open(source).convert('RGBA')
            size=Image.open(ROOT/'data/gfx'/filename).size
            assert image.size==(size[0]*4,size[1]*4)
            image.save(OUTPUT/filename)
            frames.append(dict(id=Path(filename).stem,width=size[0],height=size[1],scale=4,
                recipe='recovered original area marker: native RGBA; original eight-frame sequence',
                sources=[dict(path=str(source.relative_to(ROOT)),sha256=sha(source),native_size=list(image.size))],
                layers=[dict(file=filename,role='base',sha256=sha(OUTPUT/filename))]))
    (OUTPUT/'manifest.json').write_text(json.dumps(dict(version=1,frames=frames),indent=2)+'\n')
    print('Exported all 24 area-marker frames from original 128px images')
if __name__=='__main__':main()
