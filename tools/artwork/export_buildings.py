#!/usr/bin/env python3
"""Package verified original building groups into unchanged logical canvases."""
import hashlib,json
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[2]
NATIVE=ROOT/'datasrc/gfx/derived/building-native'
OUTPUT=ROOT/'datasrc/gfx/derived/buildings-v1'
RECIPES=ROOT/'datasrc/gfx/provenance/building-runtime-recipes.json'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    native=json.loads((NATIVE/'manifest.json').read_text())
    assert native['recipe_sha256']==sha(RECIPES),'Regenerate native building groups after changing recipes'
    expected=json.loads(RECIPES.read_text())['frames']
    assert {f['id'] for f in native['frames']}=={f['id'] for f in expected}
    OUTPUT.mkdir(parents=True,exist_ok=True)
    frames=[]
    for record in native['frames']:
        original=ROOT/record['source'];assert sha(original)==record['source_sha256']
        logical=Image.open(ROOT/'data/gfx'/(record['id']+'.png')).size
        sources=[dict(path=record['source'],sha256=record['source_sha256'],native_size=record['native_size'],layers=record['source_layers']),dict(path=str(RECIPES.relative_to(ROOT)),sha256=sha(RECIPES))]
        layers=[]
        assert {l['role'] for l in record['exports']}=={'base','team'}
        for layer in record['exports']:
            path=NATIVE/layer['file'];assert sha(path)==layer['sha256']
            image=Image.open(path).convert('RGBA');assert list(image.size)==record['native_size']
            size=Image.open(ROOT/'data/gfx'/layer['file']).size
            result=image.resize((size[0]*4,size[1]*4),Image.Resampling.LANCZOS)
            cap=image.getchannel('A').getextrema()[1]
            # Lanczos edge ringing must not increase saved translucent opacity.
            result.putalpha(result.getchannel('A').point(lambda alpha:min(alpha,cap)))
            result.save(OUTPUT/layer['file'])
            layers.append(dict(file=layer['file'],role=layer['role'],sha256=sha(OUTPUT/layer['file']),native_file=str(path.relative_to(ROOT)),alpha_ceiling=cap,selected_layers=layer['selected_layers']))
            sources.append(dict(path=str(path.relative_to(ROOT)),sha256=sha(path),native_size=record['native_size'],role=layer['role']))
        frames.append(dict(id=record['id'],width=logical[0],height=logical[1],scale=4,recipe='recovered original building: selected GIMP base/team groups; saved opacity and native alpha',sources=sources,layers=layers))
    (OUTPUT/'manifest.json').write_text(json.dumps(dict(version=1,frames=frames),indent=2)+'\n')
    print('Exported %d verified original building frames; no AI'%len(frames))
if __name__=='__main__':main()
