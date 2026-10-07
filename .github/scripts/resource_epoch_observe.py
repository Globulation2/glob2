#!/usr/bin/env python3
"""Evidence-only named-field map observer; never writes accepted goldens."""
import argparse, hashlib, json, subprocess, os
from pathlib import Path


def parse(path):
    stack, pending, header, cells, teams = [], None, {}, {}, {}
    wanted={'terrain','resourceType','resourceStock','ressource'}
    with path.open() as stream:
        for line in stream:
            s=line.strip()
            if not s: continue
            if s=='{':
                if pending is None: raise ValueError('Unnamed section')
                stack.append(pending);pending=None;continue
            if s=='}':
                if not stack: raise ValueError('Unbalanced section')
                stack.pop();continue
            if '=' not in s:
                pending=s;continue
            key,value=s.removesuffix(';').split('=',1);key=key.strip();value=value.strip()
            if stack==['Game','Map'] and key in ('wDec','hDec','undermap'):
                if key in header:raise ValueError('Repeated map field')
                header[key]=value
            if len(stack)==4 and stack[:3]==['Game','Map','cases'] and key in wanted:
                row=cells.setdefault(int(stack[3]),{})
                if key in row:raise ValueError('Repeated cell field')
                row[key]=value
            if len(stack)==4 and stack[:2]==['Game','teams'] and stack[3]=='Team' and key in ('startPosX','startPosY'):
                row=teams.setdefault(int(stack[2]),{})
                if key in row:raise ValueError('Repeated team field')
                row[key]=int(value)
    if stack:raise ValueError('Truncated map text')
    w,h=1<<int(header['wDec']),1<<int(header['hDec']);size=w*h
    um=list(bytes.fromhex(header['undermap']))
    if len(um)!=size or sorted(cells)!=list(range(size)):raise ValueError('Incomplete map cell inventory')
    terrain,source,amount=[],[],[]
    for i in range(size):
        row=cells[i];terrain.append(int(row['terrain']))
        if 'ressource' in row:
            raw=bytes.fromhex(row['ressource'])
            if len(raw)!=4:raise ValueError('Invalid legacy resource bytes')
            kind,stock=raw[0],raw[2]
        else:kind,stock=int(row['resourceType']),int(row['resourceStock'])
        source.append(255 if kind==65535 else kind);amount.append(stock)
    if sorted(teams)!=list(range(len(teams))) or not teams:raise ValueError('Incomplete team inventory')
    starts=[[teams[i]['startPosX'],teams[i]['startPosY']] for i in range(len(teams))]
    def fnv(values):
        state=14695981039346656037
        for value in values:state=((state^value)*1099511628211)&((1<<64)-1)
        return state
    full=[w,h];shape=[w,h]
    for i in range(size):full.extend((um[i],terrain[i],source[i],amount[i]));shape.extend((um[i],terrain[i],source[i]))
    tail=[len(starts)]+[v for xy in starts for v in xy];full+=tail;shape+=tail
    arrays={'undermap':um,'terrain':terrain,'sources':source,'amounts':amount,'starts':starts}
    return {'width':w,'height':h,'teams':len(starts),'full':fnv(full),'topology':fnv(shape),'sha256':{k:hashlib.sha256(json.dumps(v,separators=(',',':')).encode()).hexdigest() for k,v in arrays.items()},'arrays':arrays}


def collect(binary,cwd,output,requests,epoch,platform):
    binary=binary.resolve();cwd=cwd.resolve();output=output.resolve();output.mkdir(parents=True,exist_ok=False)
    summary={'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'platform':platform,'epoch':epoch,'anchor_table_sha256':hashlib.sha256(requests.read_bytes()).hexdigest(),'cases':[]}
    cases=json.loads(requests.read_text())['cases']
    if len(cases)!=7 or len({c['name'] for c in cases})!=7:raise ValueError('Expected seven unique diagnostic cases')
    for case in cases:
        dest=output/case['name'];dest.mkdir()
        command=[str(binary),str(case['method']),str(case['seed']),str(dest/'profile'),'tuning','quality',f"width={case['w']}",f"height={case['h']}",f"teams={case['teams']}",'candidates=0','rotations=1',f'save={dest}/map','name=resource-epoch',f'dump={dest}/terrain.txt',f'result={dest}/result.json']
        command += [f'{key}={value}' for key,value in case['options'].items()]
        env=dict(os.environ,GLOB2_STUDY_EXPLAIN=str(dest/'state'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
        (dest/'command.json').write_text(json.dumps({'argv':command,'cwd':str(cwd),'diagnostic_environment':{k:env[k] for k in ['GLOB2_STUDY_EXPLAIN','SDL_VIDEODRIVER','SDL_AUDIODRIVER']}},indent=2)+'\n')
        with (dest/'run.log').open('w') as log:subprocess.run(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=600)
        report=json.loads((dest/'result.json').read_text())
        if report['revision']!=case['revision']:raise ValueError('Generator revision mismatch')
        generated=parse(dest/'state-generated.txt');reloaded=parse(dest/'state-reloaded.txt')
        if (generated['width'],generated['height'],generated['teams']) != (1<<case['w'],1<<case['h'],case['teams']):raise ValueError('Observed dimensions/team count differ from request')
        if generated!=reloaded:raise ValueError('Generated/reloaded observed map components differ')
        anchor=case['anchors'][platform][epoch]
        if generated['full']!=anchor:raise ValueError(f'Historical/current golden anchor mismatch: {generated["full"]} != {anchor}')
        (dest/'components.json').write_text(json.dumps(generated,separators=(',',':'))+'\n')
        summary['cases'].append({'name':case['name'],'request':case,'full':generated['full'],'topology':generated['topology'],'sha256':generated['sha256'],'anchor_matches':True,'generated_reloaded_match':True})
    if hashlib.sha256(binary.read_bytes()).hexdigest()!=summary['binary_sha256']:raise ValueError('Binary mutated')
    if hashlib.sha256(requests.read_bytes()).hexdigest()!=summary['anchor_table_sha256']:raise ValueError('Anchor table mutated')
    (output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('binary',type=Path);p.add_argument('cwd',type=Path);p.add_argument('output',type=Path);p.add_argument('requests',type=Path);p.add_argument('epoch',choices=['current','historical']);p.add_argument('platform');a=p.parse_args();collect(a.binary,a.cwd,a.output,a.requests,a.epoch,a.platform)
