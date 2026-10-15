"""Root-gated standalone weighted placement qualification and screening.
Never linked into engine. All comparisons conditional on fixed micro workloads.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,os,signal,statistics,subprocess,time,itertools
p=Path(__file__).resolve().parent;w=p/'weighted-followup';ap=argparse.ArgumentParser();ap.add_argument('phase',choices=['build','correctness','timing']);a=ap.parse_args()
def sha(f):return hashlib.sha256(Path(f).read_bytes()).hexdigest()
def save(f,data):
 t=f.with_suffix('.tmp');t.write_text(json.dumps(data,indent=2)+'\n');t.replace(f)
recipe=json.loads((w/'probe-ready.json').read_text());approved=json.loads((w/'root-approved.json').read_text());review=json.loads((w/'independent-review.json').read_text());source=Path(recipe['source']);assert approved['state']=='approved_for_gated_compile_correctness_and_screening_only';assert 'static_clear' in review['status'];assert sha(source)==recipe['source_sha256']==approved['source_sha256']==review['source_sha256']
gate=json.loads((p/'timing-gate.json').read_text());assert gate['state']=='stable_boundary_ready_for_coordination_timing'
old=Path('/proc')/str(gate['old_campaign_runner_pid']);assert b'expanded-runner.py' in (old/'cmdline').read_bytes();assert (old/'stat').read_text().split(') ',1)[1].split()[0]=='T','Winner campaign must be paused between measured cohorts'
env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};binary=w/'weighted-micro';allocation=[12,13,14,28,29,30]
def run(cmd,where,timeout=60,cpus=None):
 where.mkdir(parents=True,exist_ok=True);command=(['taskset','-c',','.join(map(str,cpus))] if cpus else[])+list(map(str,cmd));start=time.monotonic();expired=False;sent=None
 with (where/'stdout.log').open('w') as log:
  child=subprocess.Popen(command,cwd=w,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  while True:
   pid,status,usage=os.wait4(child.pid,os.WNOHANG)
   if pid:break
   now=time.monotonic()
   if now-start>timeout and not expired:os.killpg(child.pid,signal.SIGTERM);expired=True;sent=now
   if expired and now-sent>5:
    try:os.killpg(child.pid,signal.SIGKILL)
    except ProcessLookupError:pass
   time.sleep(.01)
 child.returncode=os.waitstatus_to_exitcode(status);record={'command':command,'returncode':child.returncode,'timeout':expired,'process_wall_seconds':time.monotonic()-start,'user_cpu_seconds':usage.ru_utime,'system_cpu_seconds':usage.ru_stime,'total_cpu_seconds':usage.ru_utime+usage.ru_stime,'peak_rss_kib':usage.ru_maxrss,'voluntary_context_switches':usage.ru_nvcsw,'involuntary_context_switches':usage.ru_nivcsw,'source_sha256':sha(source),'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
 save(where/'receipt.json',record);assert not expired and not child.returncode,(where,record)
 return record
if a.phase=='build':
 command=list(recipe['compile_command']);command.insert(command.index('-o'),'-L/usr/lib/gcc/x86_64-linux-gnu/15');run(command,w/'build',300,list(range(32)));save(w/'build-complete.json',{'source_sha256':sha(source),'binary_sha256':sha(binary),'binary':str(binary),'compiler_command':command});raise SystemExit()
build=json.loads((w/'build-complete.json').read_text());assert build['source_sha256']==sha(source) and build['binary_sha256']==sha(binary)
matrix=recipe['initial_correctness_matrix'] if a.phase=='correctness' else recipe['staged_timing'];policies=matrix['policies'];cases=[]
if a.phase=='correctness':
 for count,lanes,shape,warm,policy in itertools.product(matrix['workers'],matrix['lanes'],matrix['shapes'],matrix['warmup_per_class'],policies):cases.append(dict(workers=count,lanes=lanes,shape=shape,warm=warm,policy=policy,jobs=matrix['jobs'],iterations=matrix['iterations'],repeat=0,allocation=list(range(32)),allocation_label='all32'))
else:
 qualified=json.loads((w/'correctness-complete.json').read_text());assert qualified['binary_sha256']==sha(binary)
 for count,lanes,shape,warm,repeat,alloc_label in itertools.product(matrix['workers'],matrix['lanes'],matrix['shapes'],matrix['warmup_per_class'],range(matrix['repeats']),['all32']):
  order=policies[repeat:]+policies[:repeat]
  if repeat%2:order=list(reversed(order))
  for policy in order:cases.append(dict(workers=count,lanes=lanes,shape=shape,warm=warm,policy=policy,jobs=matrix['jobs'],iterations=matrix['iterations'],repeat=repeat,allocation=allocation if alloc_label=='3physical6SMT' else list(range(32)),allocation_label=alloc_label))
assert len(cases)==approved['expected_correctness_cases' if a.phase=='correctness' else 'expected_screening_cases'],'Matrix differs from approved plan'
manifest={'phase':a.phase,'source_sha256':sha(source),'binary_sha256':sha(binary),'cases':cases,'scope':'Standalone CPU micro only; workers count excludes producer owner; no engine optional/GPU/async contract proof. Same payloads across policies; model sees class/history only. Cold arbitrary prior and serial calibration separate; process CPU includes warmup/thread creation, inner wall excludes them. Two timing repeats of fixed workload establish no population confidence interval.'};save(w/(a.phase+'-plan.json'),manifest)
digests={};rows=[]
for i,c in enumerate(cases):
 ident='-'.join(str(c[k]) for k in ['workers','lanes','shape','warm','allocation_label','policy','repeat']);d=w/a.phase/ident;assert not (d/'receipt.json').exists(),'Never silently reuse '+str(d)
 command=[binary,c['workers'],c['jobs'],c['lanes'],c['iterations'],c['policy'],c['shape'],c['warm']];r=run(command,d,matrix['timeout_seconds'],c['allocation']);r['case']=c;r['binary_sha256']=sha(binary);r['validation_errors']=[]
 try:
  markers=[json.loads(line.split(' ',1)[1]) for line in (d/'stdout.log').read_text().splitlines() if line.startswith('WEIGHTED_RESULT ')];assert len(markers)==1,'Missing/excess result';n=markers[0];r['weighted']=n
  for key in ['workers','jobs','lanes','iterations','policy','shape']:assert n[key]==c[key],key
  assert n['warmup_per_class']==c['warm'] and n['wall_ns']>0
  key=(c['jobs'],c['lanes'],c['iterations'],c['shape']);assert digests.setdefault(key,n['digest'])==n['digest'],'Payload digest differs by policy/worker count'
 except (AssertionError,ValueError,KeyError) as error:r['validation_errors'].append(str(error))
 save(d/'receipt.json',r);assert not r['validation_errors'],r
 rows.append(r)
 save(w/'progress.json',{'phase':a.phase,'completed':i+1,'planned':len(cases),'last':c})
save(w/(a.phase+'-complete.json'),{'state':'verified','binary_sha256':sha(binary),'source_sha256':sha(source),'cases':len(rows),'distinct_payload_digests':len(digests),'phase':a.phase})
if a.phase=='timing':
 groups={}
 for r in rows:
  c=r['case'];key=tuple(c[k] for k in ['workers','lanes','shape','warm','allocation_label']);groups.setdefault(key,{}).setdefault(c['policy'],[]).append(r)
 comparisons=[]
 for key,g in groups.items():
  for reference in ['bare-steal','steal','observe-only','static-steal']:
   if reference not in g:continue
   for policy,rr in g.items():
    if policy==reference:continue
    metrics={}
    for metric in ['inner_wall_seconds','total_cpu_seconds','system_cpu_seconds','peak_rss_kib','voluntary_context_switches']:
     def value(x):return x['weighted']['wall_ns']/1e9 if metric=='inner_wall_seconds' else x[metric]
     before=statistics.mean(value(x) for x in g[reference]);after=statistics.mean(value(x) for x in rr);metrics[metric]={'reference_mean':before,'candidate_mean':after,'change_percent':100*(after/before-1) if before else None}
    comparisons.append({'cell':dict(zip(['workers','lanes','shape','warm','allocation_label'],key)),'reference':reference,'candidate':policy,'metrics':metrics,'uncertainty':'Two timing repeats of one fixed payload; no population CI.'})
 save(w/'analysis.json',{'scope':manifest['scope'],'comparisons':comparisons,'all_payload_digests_match':True,'cases':len(rows),'source_sha256':sha(source),'binary_sha256':sha(binary)})
print('Verified',a.phase,len(rows),'cases')
