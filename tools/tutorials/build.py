#!/usr/bin/env python3
"""Validate and bundle data-authored lessons into self-contained map scripts."""
import argparse
import json
import gzip
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from tutorials.content import catalog_lessons

CONTROL_ANCHORS = {'palette', 'flag:clearing', 'build:inn', 'production', 'zone:clear', 'brush', 'build:barracks', 'flag:war', 'flag:exploration', 'zone:forbidden', 'build:racetrack', 'build:market', 'build:hospital', 'zone:guard', 'build:swimmingpool', 'upgrade', 'build:defencetower', 'build:school', 'build:swarm', 'workers', 'build:stonewall'}

def validate(d):
    def require(ok, message):
        if not ok:
            raise ValueError(message)
    require(d.get('schema') == 1, 'Unsupported lesson schema')
    require('world' not in d, 'Starting worlds belong in the JavaScript generator package')
    require(isinstance(d.get('version'), int) and 1 <= d['version'] <= 1000000, 'Invalid lesson version')
    steps = d['steps']
    require(1 <= len(steps) <= 64, 'Expected 1–64 steps')
    identifiers = [d['id'], *[s['id'] for s in steps]]
    require(all(isinstance(x,str) and 0 < len(x) <= 80 and all(c.isascii() and (c.isalnum() or c in '-_') for c in x) for x in identifiers), 'Invalid stable identifier')
    require(len({s['id'] for s in steps}) == len(steps) and all(s['id'] != 'complete' for s in steps), 'Duplicate or reserved step ID')
    require(d['map'].startswith('campaigns/') and d['map'].endswith('.map.gz') and '..' not in d['map'] and '\\' not in d['map'], 'Invalid packaged map path')
    require(0 <= d['confirmTicks'] <= 3600 and 1 <= d['hintTicks'] <= 100000, 'Invalid tutorial timing')
    def validate_field(field):
        require(field in ('workers','construction','level','hp','maxHp','range','bullets') or
                any(field==f'{prefix}.{i}' for prefix,count in [('levels',17),('performance',17),('resources',12),('production',3)] for i in range(count)), 'Unknown observation field')
    targets = d['targets']
    visiting, visited = set(), set()
    def target(name):
        require(name in targets, f'Unknown target: {name}')
        require(name not in visiting, f'Cyclic target: {name}')
        if name in visited:
            return
        visiting.add(name)
        t = targets[name]
        require(t.get('kind') in ('area','building','unit'), f'Unknown target kind: {name}')
        if t['kind'] == 'area':
            require((isinstance(t.get('region'),str) and 0<len(t['region'])<=80) or (all(isinstance(t.get(k),(float,int)) and 0 <= t[k] <= 65536 for k in ('x','y','width','height')) and t['width']>0 and t['height']>0), 'Invalid area')
            if t.get('region'):
                require(all(isinstance(t.get(k,0),int) and 0<=t.get(k,0)<=1024 for k in ('offsetX','offsetY')), 'Invalid region offset')
                require(all(k not in t or isinstance(t[k],int) and 1<=t[k]<=1024 for k in ('width','height')), 'Invalid region dimensions')
        elif not t.get('from'):
            require(isinstance(t.get('team'),int) and 0 <= t['team'] <= 31, 'Invalid target team')
        require(t.get('bind') in (None,'lesson','step') and not (t.get('bind') and (t.get('from') or t.get('baseline'))), 'Invalid bound target')
        require(t.get('baseline') in (None,'lesson','step'), 'Invalid baseline')
        for f in t.get('filters',[]):
            require(f['op'] in ('eq','ne','gte','lte'), 'Unknown filter operator')
            validate_field(f['field'])
            require(isinstance(f['value'], (int,float,dict)), 'Filter requires a number or another field')
            if isinstance(f['value'],dict):
                validate_field(f['value']['field'])
                require(isinstance(f['value'].get('scale',1),(float,int)), 'Invalid field scale')
        for link in ('from','within','assignedTo'):
            if t.get(link):
                target(t[link])
        if t.get('within'):
            require(targets[t['within']]['kind']=='area','within requires an area')
        visiting.remove(name)
        visited.add(name)
    for name in targets:
        target(name)
    def condition(c):
        kind=c['type']
        if kind in ('exists','none','count','increased'):
            if kind=='count':
                require(isinstance(c.get('minimum'),int) and c['minimum']>=1, 'Invalid count threshold')
            if kind=='increased':
                validate_field(c['field'])
                require(c.get('baseline','step') in ('lesson','step'), 'Invalid improvement baseline')
                require(isinstance(c.get('delta',1),(float,int)) and c.get('delta',1)>0, 'Improvement requires a positive delta')
                require(isinstance(c.get('minimum',1),int) and c.get('minimum',1)>=1, 'Invalid improvement count')
            require(c['target'] in targets and targets[c['target']]['kind'] != 'area', 'Condition requires an entity target')
        elif kind=='zoneCoverage':
            require(c['area'] in targets and targets[c['area']]['kind']=='area', 'Zone coverage requires an area')
            require(c['zone'] in ('guard','forbidden','clear') and isinstance(c['team'],int) and 0<=c['team']<32, 'Invalid zone observation')
            require(isinstance(c['minimum'],int) and c['minimum']>=1, 'Invalid zone threshold')
        elif kind in ('all','any'):
            require(0 < len(c['conditions']) <= 16, 'Empty/oversized condition group')
            for child in c['conditions']:
                condition(child)
        else:
            require(kind=='custom' and d.get('hooks') and c.get('name'), 'Unknown condition or missing hooks')
    for step in steps:
        condition(step['complete'])
        require(all(a in CONTROL_ANCHORS for a in step['highlights']), 'Unknown control anchor')
        require(0 <= step['hint'] < len(d['hints']), 'Invalid hint index')
        for key in ('title','instruction','touchInstruction','help','touchHelp'):
            require(isinstance(step[key],str) and 0 < len(step[key]) <= 4096, f'Missing step text: {key}')
        if step.get('destination'):
            require(step['destination']['target'] in targets, 'Unknown destination target')
        for m in step.get('milestones',[]):
            if 'target' in m:
                require(m['target'] in targets, 'Unknown milestone target')
            else:
                condition(m['condition'])
        for rule in step.get('recovery',[]):
            require(0 <= rule['hint'] < len(d['hints']), 'Invalid recovery hint')
            condition(rule['when'])
    for rule in d.get('recovery',[]):
        require(0 <= rule['hint'] < len(d['hints']), 'Invalid recovery hint')
        condition(rule['when'])
    # These are intentionally the ordinary, atomically validated map effects.
    for effect in [*d.get('onStart',[]), *[e for s in steps for e in s.get('onEnter',[])]]:
        require(effect.get('type') in ('buildingChoice','flagChoice','guiElement','scenarioOrder'), 'Unsupported lesson setup effect')
        if effect.get('type')=='scenarioOrder':
            require(isinstance(effect.get('team'),int) and 0<=effect['team']<32 and isinstance(effect.get('order'),dict),'Invalid scenario order')
            order=effect['order']
            require(isinstance(order.get('type'),str),'Missing scenario order type')
            if order.get('destination'):require(order['destination'] in targets,'Unknown scenario destination')
            if order.get('buildingTarget'):require(order['buildingTarget'] in targets and targets[order['buildingTarget']]['kind']=='building','Unknown scenario building target')

    keys=set((ROOT/'data/texts.keys.txt').read_text().splitlines())
    for value in [d['title'],d['briefing'],d['completionTitle'],d['completionBody'],*d['hints'],*[s[k] for s in steps for k in ('title','instruction','touchInstruction','help','touchHelp')],*[s['destination']['label'] for s in steps if s.get('destination')]]:
        require(value in keys, f'Unregistered translation key: {value}')
    return d

def bundle(d):
    validate(d)
    hooks=(ROOT/d['hooks']).read_text() if d.get('hooks') else "function tutorialCondition() { throw new Error('Missing tutorial condition hook'); }\n"
    return '// SPDX-License-Identifier: GPL-3.0-or-later\n// Generated by tools/tutorials/build.py; edit the definition and shared runtime.\nconst lesson = '+json.dumps(d,ensure_ascii=False,separators=(',',':'))+';\n'+hooks+(ROOT/'scripts/tutorials/runtime.js').read_text()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true',help='Require generated scripts to match their definitions')
    parser.add_argument('--maps',action='store_true',help='Regenerate packaged maps using the existing game generator CLI')
    parser.add_argument('--binary',type=Path,help='Game executable (required with --maps)')
    parser.add_argument('--lessons',nargs='+',help='Restrict map generation to lesson IDs')
    args=parser.parse_args()
    if args.maps and not args.binary: parser.error('--maps requires --binary')
    if args.lessons and not args.maps: parser.error('--lessons requires --maps')
    ids=set()
    setups=set()
    prepared=[]
    for entry,d in catalog_lessons(ROOT):
        selector=entry.get('setup')
        if not isinstance(selector,int) or isinstance(selector,bool) or selector<0 or selector in setups:
            raise ValueError('Lesson setup selectors must be explicit, unique nonnegative integers')
        setups.add(selector)
        if d['id'] in ids:
            raise ValueError('Duplicate lesson ID in catalog')
        ids.add(d['id'])
        text=bundle(d)
        prepared.append((entry,d,selector,text))
    unknown=set(args.lessons or [])-ids
    if unknown:
        parser.error('Unknown lesson IDs: '+', '.join(sorted(unknown)))
    # Validate the complete selection before changing bundles or invoking generation.
    for entry,d,selector,text in prepared:
        output=ROOT/entry['script']
        if args.check:
            if not output.exists() or output.read_text()!=text:
                raise ValueError(f'Stale tutorial bundle: {output.relative_to(ROOT)}')
        else:
            output.write_text(text)
        if args.maps and (not args.lessons or d['id'] in args.lessons):
            with tempfile.TemporaryDirectory(prefix='glob2-training-') as temporary:
                temporary=Path(temporary)
                metadata=temporary/'scenario.json'
                metadata.write_text(json.dumps(dict(name=d['name'],localTeam=0,victory='scenario',briefing=d['briefing'],objectives=[s['title'] for s in d['steps']],hints=d['hints'])))
                destination=temporary/'lesson.map.gz'
                command=[str(args.binary.resolve()),'map','generate','glob2:training','--generator-package',str(ROOT/'data/generators/training'),'--seed','20261026','--width','256','--height','256','--teams','2','--workers','8','--set',f'lesson={selector}', '--scenario',str(output),'--scenario-config',str(metadata),'--output',str(destination)]
                subprocess.run(command,cwd=ROOT,check=True)
                target=ROOT/d['map']
                if args.check:
                    if not target.exists() or gzip.decompress(target.read_bytes())!=gzip.decompress(destination.read_bytes()):
                        raise ValueError(f'Stale tutorial map: {target.relative_to(ROOT)}')
                else: target.write_bytes(destination.read_bytes())
        print(f'{d["id"]}: {len(d["steps"])} steps → {output.relative_to(ROOT)}')
if __name__=='__main__':
    main()
