#!/usr/bin/env python3
"""Prepare a paired parameter variant from an immutable local tournament baseline."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('baseline',type=Path)
p.add_argument('output',type=Path)
p.add_argument('--set',action='append',required=True,dest='changes')
a=p.parse_args();base=a.baseline.resolve();out=a.output.resolve()
if out.exists():p.error('Output already exists; use the tournament runner to resume it')
m=json.loads((base/'manifest.json').read_text());original=json.loads((base/'snapshot/resolved.json').read_text())
params={r['key']:r for r in original['parameters']};changes={}
for item in a.changes:
 key,sep,value=item.partition('=')
 if not sep or key not in params:p.error('Unknown parameter: '+item)
 value=json.loads(value)
 if type(value) is not type(params[key]['value']):p.error('Parameter type mismatch: '+key)
 if value==params[key]['value']:p.error('Parameter is unchanged: '+key)
 changes[key]={'baseline':params[key]['value'],'variant':value}
for rel,sha in m['hashes'].items():
 if hashlib.sha256((base/'snapshot'/rel).read_bytes()).hexdigest()!=sha:raise RuntimeError('Baseline changed: '+rel)
frozen=out/'snapshot';frozen.mkdir(parents=True)
for rel in m['hashes']:
 dest=frozen/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(base/'snapshot'/rel,dest)
for key,change in changes.items():
 params[key]['value']=change['variant'];params[key]['source']='paired experimental override'
(frozen/'resolved.json').write_text(json.dumps(original,indent=2)+'\n')
(frozen/'resolved.strategy').write_text(''.join(f"{r['key']} = {str(r['value']).lower()}\n" for r in original['parameters']))
m.update(created_utc=datetime.now(timezone.utc).isoformat(),paired_baseline=str(base),parameter_changes=changes)
for game in m['schedule']:
 original_map=Path(game['map_file']).resolve()
 relative=original_map.relative_to(base/'snapshot')
 if str(relative) not in m['hashes']:raise RuntimeError('Unfrozen map: '+str(original_map))
 game['map_file']=str(frozen/relative)
m['hashes']={rel:hashlib.sha256((frozen/rel).read_bytes()).hexdigest() for rel in m['hashes']}
(out/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
print(json.dumps({'output':str(out),'games':m['matches'],'changes':changes,'identical_binary':m['hashes']['glob2']==json.loads((base/'manifest.json').read_text())['hashes']['glob2']},indent=2))
