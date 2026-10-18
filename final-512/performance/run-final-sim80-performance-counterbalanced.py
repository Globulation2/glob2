from pathlib import Path
import json,subprocess,os,time,hashlib,datetime,random,xml.etree.ElementTree as ET
c=Path(__file__).parent; root=Path('/Users/bradley/.codex/worktrees/adaptive-final-integration/glob2');out=c/'final-performance';out.mkdir(exist_ok=True)
def save(p,x):
 t=p.with_suffix('.tmp');t.write_text(json.dumps(x,indent=2)+'\n');t.replace(p)
def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()
status=out/'status.json';save(status,dict(phase='waiting_for_local_tests',updated=stamp()))
xml=c/'final-sim80-engine-mac-qualified.xml'
while not xml.exists():time.sleep(5)
tree=ET.parse(xml)
assert not list(tree.iter('failure')) and not list(tree.iter('error')),'Final focused Mac tests failed; benchmark refused'
assert 'PASS all per-tick checksums' in (c/'final-sim80-parity-mac.log').read_text(), 'Final parity missing'
binaries={'master':c/'bin/glob2-master-3d896cbb9','adaptive32':c/'bin/glob2-final-ca6365e83'}
rows=json.loads((c/'final-performance-maps/manifest.json').read_text());random.Random(20261018).shuffle(rows)
plan=[]
for rep in range(2):
 for index,row in enumerate(rows if rep==0 else rows[::-1]):
  first='master' if index%2==0 else 'adaptive32'
  for label in [first,'adaptive32' if first=='master' else 'master']:
   plan.append(dict(repetition=rep,policy=label,**row))
save(out/'plan.json',dict(tick_cap=90000,threads=1,gpu='off',telemetry='off',benchmark_warmup=0,source=json.loads((c/'final-pr-binary.json').read_text()),binaries={k:dict(path=str(v),sha256=hashlib.sha256(v.read_bytes()).hexdigest()) for k,v in binaries.items()},games=plan))
manifest=json.loads((out/'manifest.json').read_text()) if (out/'manifest.json').exists() else []
for index,row in enumerate(plan):
 if index<len(manifest):
  old=manifest[index];assert (old['format'],old['policy'],old['repetition'],old['map_sha256'])==(row['format'],row['policy'],row['repetition'],row['map_sha256'])
  continue
 d=out/f"{index:02d}-{row['format']}-{row['policy']}-r{row['repetition']}";d.mkdir(exist_ok=True);s=row['spec']
 cmd=[str(binaries[row['policy']]),'game','run','--map-file',row['map'],'--game-seed',str(s['game_seed']),'--compute-threads','1','--gpu','off','--ticks','90000','--benchmark-warmup','0','--output-dir',str(d)]
 for ai in s['ais']:cmd+=['--player',ai]
 for a in s['alliances']:cmd+=['--alliance',str(a)]
 save(d/'command.json',cmd)
 save(status,dict(phase='running',completed=len(manifest),total=len(plan),active=d.name,updated=stamp()))
 with (d/'run.log').open('w') as f:
  started=stamp();begin=time.monotonic();p=subprocess.Popen(cmd,cwd=root,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
  save(d/'process.json',dict(pid=p.pid,started=started,command=cmd))
  _,code,usage=os.wait4(p.pid,0);p.returncode=os.waitstatus_to_exitcode(code);wall=time.monotonic()-begin
 assert p.returncode==0,(d,p.returncode)
 result=json.loads((d/'result.json').read_text());ticks=result['ticks']
 measured=result['benchmark_measured_ticks'];run_cpu=result['benchmark_run_cpu_ns']/1e9
 record=dict(index=index,format=row['format'],policy=row['policy'],repetition=row['repetition'],map_sha256=row['map_sha256'],game_seed=s['game_seed'],ticks=ticks,measured_ticks=measured,wall_seconds=wall,total_cpu_seconds=usage.ru_utime+usage.ru_stime,simulation_cpu_seconds=run_cpu,cpu_ms_per_tick=1000*run_cpu/measured,total_cpu_ms_per_tick=1000*(usage.ru_utime+usage.ru_stime)/ticks,ticks_per_wall_second=ticks/wall,termination=result.get('termination'),result=str(d/'result.json'))
 assert not list(d.glob('gradient-*.csv')),'Collector unexpectedly active'
 manifest.append(record);save(out/'manifest.json',manifest);print(json.dumps(record),flush=True)
save(status,dict(phase='complete',completed=len(manifest),total=len(plan),updated=stamp()))
