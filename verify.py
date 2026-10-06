import gzip,hashlib,json,subprocess,os,sys,shutil
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else Path(__file__).resolve().parent
out.mkdir(parents=True,exist_ok=True)
binary=Path(sys.argv[1]).resolve()
scenarios=[('small-castor',4096,2),('land-maxima',16384,4),('large-mixed',16384,4),('oazis-maxima',16384,4)]
rows=json.loads((out/"checks.json").read_text()) if (out/"checks.json").exists() else []
for scenario,start,default in scenarios:
 for label,threads,workers,mask in [('serial',1,2,'ai'),('parallel',default,2,'ai'),('fallback',1,0,'ai'),('disabled',default,2,'none')]:
  if any(r['scenario']==scenario and r['variant']==label for r in rows): continue
  dest=out/(scenario+'-'+label);dest.mkdir(exist_ok=True)
  command=[str(binary),'--run-game','--load-game',str(root/'artifacts/critical-path-prep'/(scenario+'-fixture')/'final.game.gz'),'--ticks',str(start+1024),'--compute-threads',str(threads),'--gradient-workers',str(workers),'--compute-experiments',mask,'--telemetry','checksums','--replay','true','--save','final','--output-dir',str(dest)]
  (dest/'command.json').write_text(json.dumps(command))
  with (dest/'engine.log').open('w') as log: subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
  def digest(filename):
   data=(dest/filename).read_bytes()
   if filename.endswith('.gz'):data=gzip.decompress(data)
   return hashlib.sha256(data).hexdigest()
  row={'scenario':scenario,'variant':label,'hashes':{f:digest(f) for f in ['game.replay.checksums','game.replay','final.game.gz']},'result':json.loads((dest/'result.json').read_text())}
  rows.append(row);(out/'checks.json').write_text(json.dumps(rows,indent=2))
  for filename in ['game.replay', 'game.replay.checksums']:
   source=dest/filename
   with source.open('rb') as input, gzip.open(str(source)+'.gz','wb',compresslevel=1) as output: shutil.copyfileobj(input,output)
   source.unlink()
  print(scenario,label,row['result']['finalChecksum'],flush=True)
 values=[r['hashes'] for r in rows if r['scenario']==scenario]
 assert all(v==values[0] for v in values),scenario
 print('EXACT MATCH',scenario,flush=True)
