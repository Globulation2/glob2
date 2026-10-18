from pathlib import Path
import json,subprocess,os,hashlib
c=Path(__file__).parent; binary=c/'bin/glob2-master-3d896cbb9'; out=c/'final-performance-maps'; records=[]
for row in json.loads((c/'final-performance-selected.json').read_text()):
 spec=row['spec']; folder=out/spec['format'];folder.mkdir(exist_ok=True); target=folder/'map.map.gz'
 cmd=[str(binary),'map','generate',spec['generator'],'--seed',str(spec['map_seed']),'--width',str(spec['width']),'--height',str(spec['height']),'--teams',str(spec['players']),'--output',str(target),'--report-file',str(folder/'generation.json')]
 for k,v in sorted(spec['generator_params'].items()):cmd+=['--set',f'{k}={v}']
 (folder/'command.json').write_text(json.dumps(cmd,indent=2))
 with (folder/'generation.log').open('w') as f:subprocess.run(cmd,cwd='/Users/bradley/.codex/worktrees/gradient-final-master-baseline/glob2',stdout=f,stderr=subprocess.STDOUT,check=True,timeout=600)
 records.append(dict(format=spec['format'],spec=spec,map=str(target),map_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),source_revision='3d896cbb95293da6d24caddd247a62c9ca99d77f',original_training_map_sha256=row['map_sha256'],map_group=row['map_group']))
 (out/'manifest.json').write_text(json.dumps(records,indent=2)+'\n')
 print(spec['format'],records[-1]['map_sha256'],flush=True)
