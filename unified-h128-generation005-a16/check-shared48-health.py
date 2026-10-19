import pathlib,sys,json,datetime,time,subprocess,concurrent.futures,shutil
C=pathlib.Path(__file__).resolve().parent;D=C.parent;sys.path.insert(0,str(D/'launch-tool-source'))
from tools.tournaments.transport import Transport,package_identity
from tools.tournaments.coordinator import Coordinator
from tools.tournaments.common import atomic_json
H=json.loads((C/'hosts-common-budget.json').read_text());exp=json.loads((C/'PREPARED-A16-NOT-DISPATCHED.json').read_text())['experiment']
def check(h):
 t=Transport(h,C/'collection/worker.pyz');t.deployed=True
 try:
  state=t.rpc('status');root=str(pathlib.PurePosixPath(h['directory'])/'workers'/package_identity());allowed=[str(pathlib.PurePosixPath(root)/'bundles'/b/'bin/glob2') for b in h['builds']]
  code='''import pathlib,json,subprocess,sys,time,os,shlex,shutil,hashlib
root=pathlib.Path(sys.argv[1]);allowed=set(json.loads(sys.argv[2]));exp=sys.argv[3];rows=[];binary_hashes={};hz=os.sysconf('SC_CLK_TCK');uptime=float(pathlib.Path('/proc/uptime').read_text().split()[0])
for line in subprocess.check_output(['ps','-Aww','-o','pid=,command='],text=True).splitlines():
 parts=line.strip().split(None,1)
 if len(parts)!=2 or parts[1].split(None,1)[0] not in allowed:continue
 args=shlex.split(parts[1])
 if not args or args[0] not in allowed:continue
 pid=int(parts[0]);out=pathlib.Path(args[args.index('--output-dir')+1]);attempt=json.loads((out.parent/'attempt.json').read_text());assert attempt['experiment'] in {exp,'gradient-7cdcf09e7c712271a59ef7fd'}
 fields=pathlib.Path('/proc',str(pid),'stat').read_text().rsplit(')',1)[1].split();age=uptime-int(fields[19])/hz;cpu=(int(fields[11])+int(fields[12]))/hz;last=None
 if (out/'progress.jsonl').exists():
  for raw in (out/'progress.jsonl').read_text().splitlines():
   try:last=json.loads(raw)
   except ValueError:pass
 if args[0] not in binary_hashes:binary_hashes[args[0]]=hashlib.sha256(pathlib.Path(args[0]).read_bytes()).hexdigest()
 rows.append(dict(experiment=attempt['experiment'],model_sha256=attempt['job']['labels']['resource_gradient']['cohort']['policy']['model_sha256'],pid=pid,binary_sha256=binary_hashes[args[0]],declared_threshold_milli=attempt['job']['labels']['resource_gradient']['cohort']['policy']['threshold_milli'],attempt=attempt['id'],job=attempt['job']['id'],actual_age_seconds=age,process_cpu_seconds=cpu,average_cpu_percent=100*cpu/age if age>0 else None,progress=last,timeout_seconds=attempt['job']['limits']['timeout_seconds'],output=str(out)))
print(json.dumps(dict(engine_count=len(rows),engines=rows,free_bytes=shutil.disk_usage(root).free,loadavg=pathlib.Path('/proc/loadavg').read_text().strip())))'''
  actual=json.loads(t.call([t.python,'-c',code,root,json.dumps(allowed),exp]).decode());return dict(name=h['name'],slots=h['slots'],worker_status=state,actual=actual)
 finally:t.close()
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as p:hosts=list(p.map(check,H))
c=Coordinator(C/'collection',repair=False);s=c.status();c.close();local=[l for l in subprocess.check_output(['ps','-Aww','-o','pid=,command='],text=True).splitlines() if '/gradient-unified-20261019/round-' in l and '/fleet-local/' in l and '/bin/glob2 game run ' in l]
assert not local and len(hosts)==4 and all(h['actual']['engine_count']<=h['slots'] for h in hosts)
r=dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),experiment=exp,coordinator_pid=14528,jobs=s['jobs'],failures=s['failures'],hosts=hosts,actual_remote_engines=sum(h['actual']['engine_count'] for h in hosts),total_slot_budget=48,mac_collection_engines=local,local_free_bytes=shutil.disk_usage(D).free,oldest_actual_age_seconds=max([e['actual_age_seconds'] for h in hosts for e in h['actual']['engines']]or[0]),caps_all1800=all(e['timeout_seconds']==1800 for h in hosts for e in h['actual']['engines']))

c=Coordinator(D/'exploration-remote-only',repair=False);old=c.status();c.close();r['exploration_jobs']=old['jobs'];r['engines_by_experiment']={x:sum(e['experiment']==x for h in hosts for e in h['actual']['engines']) for x in [exp,'gradient-7cdcf09e7c712271a59ef7fd']}
assert all(e['declared_threshold_milli']==16000 and e['binary_sha256']==('5a5c74ece56f23507f137a871b076dbbc667c741b83f60caeee0174ed0c6e7fe' if e['experiment']==exp else 'ffbbff5e07525d3c208ca6d5a563a77c34595419d8de88e3cb0cebe337b47abc') for h in hosts for e in h['actual']['engines'])
atomic_json(C/'LATEST-SHARED48-HEALTH.json',r)
print(json.dumps({k:v for k,v in r.items() if k!='hosts'},sort_keys=True));print([(h['name'],h['actual']['engine_count'],round(h['actual']['free_bytes']/2**30,2),min([e['progress'].get('tick',0) for e in h['actual']['engines'] if e['progress']]or[0]),max([e['progress'].get('tick',0) for e in h['actual']['engines'] if e['progress']]or[0])) for h in hosts])
