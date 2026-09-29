from pathlib import Path
import concurrent.futures,subprocess,json,random,sys,time,platform,hashlib,gzip,shutil
R=Path.cwd();D=R/'artifacts/gradient-eight-player';P=R/'artifacts/gradient-production';host=sys.argv[1];slots=int(sys.argv[2]);cases=[c for c in json.loads((D/'cases.json').read_text()) if c['host']==host];binaries={v:P/('glob2-'+v) for v in ['baseline','candidate']}
manifest={v:hashlib.sha256((Path(str(p)+'.bin') if Path(str(p)+'.bin').exists() else p).read_bytes()).hexdigest() for v,p in binaries.items()};(D/f'manifest-{host}.json').write_text(json.dumps(manifest,indent=2))
def run(c):
 variants=['baseline','candidate'];random.Random(c['id']).shuffle(variants)
 for v in variants:
  out=D/'games'/c['id']/v
  if (out/'completed.json').exists():continue
  if (out/'result.json').exists():raise RuntimeError('Existing unfinalized result; preserve and inspect '+str(out))
  assert shutil.disk_usage(D).free>8*1024**3,'Less than8GiB free: refusing new job'
  out.mkdir(parents=True,exist_ok=True)
  cmd=[str(binaries[v]),'--run-game','--map-file',str(D/c['map']),'--game-seed',str(c['seed'])]
  for ai in c['players']:cmd+=['--player',ai]
  cmd+=['--ticks','65536','--output-dir',str(out),'--telemetry','team-timeline','--replay','false']
  if c['base_seed']==10501:cmd+=['--save','every:32768']
  (out/'command.json').write_text(json.dumps(cmd));start=time.monotonic();prefix=['/usr/bin/time','-l'] if platform.system()=='Darwin' else ['/usr/bin/time','-f','%U %S %e %M','-o',str(out/'cpu.txt')]
  with gzip.open(out/'stdout.log.gz','wt',compresslevel=1) as f:
   p=subprocess.Popen(prefix+cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
   for line in p.stdout:
    if line.startswith(('GLOB2_ECON ','GLOB2_MEASURE ','GLOB2_PERF_FINAL ')) or 'error' in line.lower() or ' user ' in line or 'resident' in line:f.write(line)
   status=p.wait()
  if status:raise RuntimeError(f'{c["id"]} {v} exit {status}')
  result=json.loads((out/'result.json').read_text());assert len(result['teams'])==8
  saves=[]
  for save in out.glob('checkpoint-*.game'):
   with save.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
   compressed=Path(str(save)+'.gz')
   with save.open('rb') as inp,gzip.open(compressed,'wb',compresslevel=1) as target:shutil.copyfileobj(inp,target)
   with gzip.open(compressed,'rb') as f:assert hashlib.file_digest(f,'sha256').hexdigest()==digest
   saves.append({'file':compressed.name,'uncompressed_sha256':digest});save.unlink()
  (out/'completed.json').write_text(json.dumps({'elapsed':time.monotonic()-start,'ticks':result['ticks'],'saves':saves}));print('DONE',c['id'],v,result['ticks'],round(time.monotonic()-start,1),flush=True)
random.Random(106001).shuffle(cases)
with concurrent.futures.ThreadPoolExecutor(max_workers=slots) as pool:list(pool.map(run,cases))
print('HOST_DONE',host,2*len(cases),flush=True)
