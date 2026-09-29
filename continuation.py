#!/usr/bin/env python3
"""Compare freshly loaded checkpoints with both variants and uninterrupted traces."""
import argparse,gzip,hashlib,importlib.util,json,pathlib,subprocess
p=argparse.ArgumentParser();p.add_argument('--root',type=pathlib.Path,required=True);p.add_argument('--verification',type=pathlib.Path,required=True);p.add_argument('--baseline',type=pathlib.Path,required=True);p.add_argument('--candidate',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
a.root=a.root.resolve();a.verification=a.verification.resolve();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=False)
spec=importlib.util.spec_from_file_location('comparison',a.root/'test/compare_save_continuation.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
rows=[]
for name,start in [('continents12',12000),('islands12',12000),('even12',12000),('held-even12-late',40000)]:
 source=a.verification/(name+'-baseline');save=source/f'checkpoint-{start+128}.game.gz';reference=dict(m.records(gzip.decompress((source/'game.replay.checksums.gz').read_bytes())));digests=[]
 for label,binary in [('baseline',a.baseline),('candidate',a.candidate)]:
  out=a.output/(name+'-'+label);out.mkdir();cmd=[str(binary.resolve()),'--run-game','--load-game',str(save),'--ticks',str(start+256),'--gradient-workers','1','--gradient-delay','8','--output-dir',str(out),'--telemetry','checksums','--replay','true','--save','final'];(out/'command.json').write_text(json.dumps(cmd))
  with (out/'stdout.log').open('w') as f:subprocess.run(cmd,cwd=a.root,stdout=f,stderr=subprocess.STDOUT,check=True)
  trace=(out/'game.replay.checksums').read_bytes();resumed=list(m.records(trace));assert len(resumed)==128
  mismatch=next((tick for tick,data in resumed if reference[tick]!=data),None);digest=hashlib.sha256(trace).hexdigest();digests.append(digest)
  row={'case':name,'variant':label,'ticks':len(resumed),'trace_sha256':digest,'first_uninterrupted_mismatch':mismatch};rows.append(row);print(row,flush=True)
  for path in [out/'game.replay.checksums',out/'game.replay']:
   with gzip.open(str(path)+'.gz','wb',compresslevel=1) as f:f.write(path.read_bytes())
   path.unlink()
 assert len(set(digests))==1,name
 (a.output/'results.json').write_text(json.dumps(rows,indent=2))
