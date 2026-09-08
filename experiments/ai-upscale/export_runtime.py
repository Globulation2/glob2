#!/usr/bin/env python3
"""Export reviewed artwork into a self-contained, versioned runtime pack."""
import hashlib
import json
from pathlib import Path
import shutil
import numpy as np
import connected_terrain
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
EXP=ROOT/'experiments/ai-upscale'
OUT=ROOT/'data/highres/v1'

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    frames=json.loads((EXP/'finishing/manifest.json').read_text())
    config=json.loads((EXP/'finishing.json').read_text())
    for f in frames:
        selected=config['images'].get(f['id'],{}).get('selected',config['default'])
        assert f['selected']==selected, f"Stale finishing output for {f['id']}; rerun finishing.py"
    records=[]
    for f in frames:
        method=f['selected']; layers=[]
        if method.startswith('generator_'):
            path=EXP/next(v['path'] for v in f['variants'] if v['id']==method)
            a=np.asarray(Image.open(path).convert('RGBA')).copy()
            # Neutral extracted shadows are excluded from the hue-shift layer.
            rgb=a[:,:,:3].astype(float)
            green=(rgb[:,:,1]>rgb[:,:,0]+8)&(rgb[:,:,1]>rgb[:,:,2]+8)
            base=a.copy();team=a.copy();base[green]=0;team[~green]=0
            for name,pixels,role in [(f['id']+'.png',base,'base'),(f['id']+'r.png',team,'team')]:
                Image.fromarray(pixels).save(OUT/name)
                layers.append(dict(file=name,role=role,sha256=sha(OUT/name),source_sha256=sha(path)))
            composed=Image.alpha_composite(Image.fromarray(base),Image.fromarray(team))
            assert np.array_equal(np.asarray(composed),a)
        else:
            for name in f['layers']:
                source=EXP/('buildings/corrected' if method=='current' else 'finishing/layers/'+method)/name
                shutil.copyfile(source,OUT/name)
                layers.append(dict(file=name,role='team' if name.endswith('r.png') else 'base',sha256=sha(OUT/name),source_sha256=sha(source)))
        records.append(dict(id=f['id'],width=f['width'],height=f['height'],scale=4,layers=layers,recipe=method))
    for i in range(272):
        name=f'terrain{i}.png';source=EXP/'connected-terrain'/name;Image.open(source).convert('RGBA').save(OUT/name)
        w,h=Image.open(ROOT/'data/gfx'/name).size
        records.append(dict(id=f'terrain{i}',width=w,height=h,scale=4,recipe='shared-material rugged corner masks v4; sparse large grass tufts',layers=[dict(file=name,role='base',sha256=sha(OUT/name),source_sha256=sha(source))]))
    resource_sources=sorted((EXP/'resources/corrected').glob('ressource[0-9]*.png'),key=lambda p:int(p.stem[9:]))
    assert len(resource_sources)==65, 'Run upscale_resources.py before export'
    for source in resource_sources:
        name=source.name
        shutil.copyfile(source,OUT/name)
        w,h=Image.open(ROOT/'data/gfx'/name).size
        records.append(dict(id=source.stem,width=w,height=h,scale=4,recipe='resource constrained',layers=[dict(file=name,role='base',sha256=sha(OUT/name),source_sha256=sha(source))]))
    resource_levels=[]
    for level in range(4):
        slot=256>>level; border=32>>level
        atlas=Image.new('RGBA',(slot*8,slot*9))
        for i,source in enumerate(resource_sources):
            tile=Image.open(source).convert('RGBA')
            tile=tile.resize((tile.width>>level,tile.height>>level),Image.Resampling.LANCZOS)
            padded=Image.fromarray(np.pad(np.asarray(tile),((border,border),(border,border),(0,0)),mode='edge'))
            atlas.paste(padded,((i%8)*slot,(i//8)*slot))
        name=f'ressource-atlas-mip{level}.png';atlas.save(OUT/name)
        resource_levels.append(dict(file=name,sha256=sha(OUT/name)))
    world={}
    for source in sorted((EXP/'world/corrected').glob('*.png')):
        if source.stem.startswith('terrain'):continue
        frame_id=source.stem.removesuffix('r')
        world.setdefault(frame_id,[]).append(source)
    for frame_id,sources in world.items():
        layers=[];w=h=0
        for source in sources:
            if source.name=='water0.png':source=EXP/'materials/water0.png'
            name=source.name;Image.open(source).convert('RGBA').save(OUT/name)
            lw,lh=Image.open(ROOT/'data/gfx'/name).size;w=max(w,lw);h=max(h,lh)
            layers.append(dict(file=name,role='team' if source.stem.endswith('r') else 'base',sha256=sha(OUT/name),source_sha256=sha(source)))
        logical_source=ROOT/'data/gfx'/(frame_id+'.png')
        if not logical_source.exists():logical_source=ROOT/'data/gfx'/(frame_id+'r.png')
        w,h=Image.open(logical_source).size
        method='world constrained' if frame_id.startswith(('water','bullet','explosion','magiceffect','particle')) else 'soft mask resampling'
        if frame_id=='water0':method='generated ripples; periodic material v1'
        records.append(dict(id=frame_id,width=w,height=h,scale=4,recipe=method,layers=layers))
    lines=['GLOB2_HIGHRES 1']
    for f in records:
        layer={x['role']:x['file'] for x in f['layers']}
        for x in f['layers']:
            im=Image.open(OUT/x['file'])
            original=ROOT/'data/gfx'/x['file']
            expected=Image.open(original).size if original.exists() and not f['recipe'].startswith('generator_') else (f['width'],f['height'])
            assert im.size==(expected[0]*4,expected[1]*4)
            x['logical_width'],x['logical_height']=expected
            x['original_sha256']=sha(original) if original.exists() else None
        lines.append(f"{f['id']} {f['width']} {f['height']} 4 {layer.get('base','-')} {layer.get('team','-')}")
    atlas_levels=[]
    for level in range(4):
        slot=256>>level; border=64>>level; size=128>>level
        tiles=connected_terrain.mip_tiles(level)
        atlas=Image.new('RGBA',(slot*16,slot*17))
        for i in range(272):
            tile=Image.fromarray(tiles[i])
            padded=Image.fromarray(np.pad(np.asarray(tile),((border,border),(border,border),(0,0)),mode='edge'))
            atlas.paste(padded,((i%16)*slot,(i//16)*slot))
        name=f'terrain-atlas-mip{level}.png';atlas.save(OUT/name)
        atlas_levels.append(dict(file=name,sha256=sha(OUT/name)))
    (OUT/'frames.txt').write_text('\n'.join(lines)+'\n')
    (OUT/'manifest.json').write_text(json.dumps(dict(version=1,selection_sha256=sha(EXP/'finishing.json'),frames=records,terrain_atlas=dict(slot=256,border=64,columns=16,rows=17,levels=atlas_levels),resource_atlas=dict(slot=256,border=32,columns=8,rows=9,levels=resource_levels)),indent=2)+'\n')
    print(f'Exported {len(records)} frames, {sum(len(f["layers"]) for f in records)} layers to {OUT}')

if __name__=='__main__':main()
