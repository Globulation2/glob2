#!/usr/bin/env python3
"""Inventory building registry and run the existing constrained 4x pipeline."""
import json
import re
from pathlib import Path
import super_resolution as pipeline

ROOT = pipeline.ROOT
OUT = ROOT/'experiments/ai-upscale/buildings'
LABELS = {'swarm':'Swarm','inn':'Inn','hospital':'Hospital','market':'Market',
          'racetrack':'Racetrack','swimmingpool':'Swimming pool','pool':'Swimming pool',
          'barracks':'Barracks','school':'School','defencetower':'Defense tower',
          'stonewall':'Stone wall','explorationflag':'Exploration flag',
          'warflag':'War flag','clearingflag':'Clearing flag'}


def inventory():
    registry=[]
    for path in sorted((ROOT/'src/game/entities').glob('BuildingTypes*.cpp')):
        for block in re.split(r'(?=\{\s*\.type\s*=)',path.read_text())[1:]:
            def number(key,default=0):
                m=re.search(r'\.'+key+r'\s*=\s*(-?\d+)',block)
                return int(m[1]) if m else default
            kind=re.search(r'\.type\s*=\s*"([^"]+)"',block)[1]
            base=re.search(r'\.gameSprite\s*=\s*"data/gfx/([^"]+)"',block)[1]
            registry.append(dict(family=LABELS.get(kind,kind.title()), kind=kind, base=base,
                                 level=number('level')+1, construction=bool(number('isBuildingSite')),
                                 virtual=bool(number('isVirtual')), connected=bool(number('crossConnectMultiImage')),
                                 start=number('gameSpriteImage'), count=number('gameSpriteCount',1),
                                 source=str(path.relative_to(ROOT))))
    frames={}
    for row in registry:
        for path in sorted((ROOT/'data/gfx').glob(row['base']+'*.png')):
            match=re.fullmatch(re.escape(row['base'])+r'(\d+)(r?)\.png',path.name)
            if not match: continue
            key=row['base']+match[1]
            frame=frames.setdefault(key,dict(id=key,base=row['base'],index=int(match[1]),layers=[],uses=[]))
            if path.name not in frame['layers']:frame['layers'].append(path.name)
    for row in registry:
        for index in range(row['start'],row['start']+(16 if row['connected'] else row['count'])):
            frame=frames.get(row['base']+str(index))
            assert frame,('Missing registry frame',row,index)
            if row['connected']:
                directions=[name for bit,name in [(8,'N'),(4,'S'),(2,'W'),(1,'E')] if index & bit]
                state='Connections '+(' / '.join(directions) if directions else 'none')
            elif row['virtual']:state='Flag'
            elif row['construction']:state='Construction'
            elif index==row['start']:state='Completed'
            else:state=f'Damage {index-row["start"]}/{row["count"]-1}'
            frame['uses'].append(dict(family=row['family'],level=row['level'],state=state,kind=row['kind']))
    # Include all matching extra on-disk building series, even if not referenced
    # by today's registry. Label them explicitly rather than silently skipping.
    roots=('swarm','inn','hosp','market','racetrack','pool','barracks','school','defencetower','wall','buildingsite')
    known={name for f in frames.values() for name in f['layers']}
    for path in sorted((ROOT/'data/gfx').glob('*.png')):
        if path.name in known or not path.name.startswith(roots): continue
        match=re.fullmatch(r'(.+?)(\d+)(r?)\.png',path.name)
        if not match:continue
        key=match[1]+match[2]
        f=frames.setdefault(key,dict(id=key,base=match[1],index=int(match[2]),layers=[],uses=[]))
        f['layers'].append(path.name)
    for f in frames.values():
        f['layers'].sort()
        f['family']=f['uses'][0]['family'] if f['uses'] else 'Additional source assets'
        if f['base']=='buildingsite':f['family']='Shared construction sites'
        f['label']='; '.join(f'{u["family"]} L{u["level"]} · {u["state"]}' for u in f['uses']) or 'Not referenced by current building registry'
    return registry,list(frames.values())


if __name__=='__main__':
    OUT.mkdir(parents=True,exist_ok=True)
    registry,frames=inventory()
    names=sorted({name for f in frames for name in f['layers']})
    manifest=dict(registry_entries=len(registry),unique_frames=len(frames),layer_count=len(names),
                  scope='All map building series from BuildingTypes*.cpp, all loaded states and matching extra source assets; includes virtual flags, excludes miniature UI icons.',
                  registry=registry,frames=frames)
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'{len(registry)} registry entries; {len(frames)} distinct frames; {len(names)} PNG layers',flush=True)
    pipeline.OUT=OUT
    pipeline.NAMES=names
    pipeline.prepare()
    pipeline.finish(pipeline.run(),make_preview=False)
