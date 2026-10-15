"""Frozen-source deadline/optional latency qualification and timing, root gate only."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,shlex,signal,statistics,subprocess,time
from process_cleanup import kill_owned_tree
p=Path(__file__).resolve().parent;d=p/'deadline-latency';workspace=p.parents[2];ap=argparse.ArgumentParser();ap.add_argument('phase',choices=['build','qualify','timing']);a=ap.parse_args();manifest=json.loads((d/'manifest.json').read_text())
def sha(f):return hashlib.sha256(Path(f).read_bytes()).hexdigest()
def save(f,x):
 t=f.with_suffix('.tmp');t.write_text(json.dumps(x,indent=2)+'\n');t.replace(f)
assert sha(d/'probe.cpp')==manifest['harness_sha256']
gate=json.loads((p/'timing-gate.json').read_text());assert gate['state']=='stable_boundary_ready_for_coordination_timing'
old=Path('/proc')/str(gate['old_campaign_runner_pid']);assert b'expanded-runner.py' in (old/'cmdline').read_bytes() and (old/'stat').read_text().split(') ',1)[1].split()[0]=='T','Campaign must actually be paused'
local=json.loads((p/'local-completion-ready.json').read_text());first=json.loads((p/'candidate-ready.json').read_text());base=Path('/home/bradley/.codex/worktrees/engine-history-latest-master/glob2');sources={'baseline':base,'first-steal':Path(first['source']),'local':Path(local['source'])};headers={k:sha(v/'src/common/ComputeExecutor.h') for k,v in sources.items()}
assert headers['first-steal']==first['source_hashes']['src/common/ComputeExecutor.h'];assert headers['local']==local['source_hashes']['src/common/ComputeExecutor.h']
priority=json.loads((d/'priority-header-ready.json').read_text());assert 'independently_static_clear' in priority['status'];assert sha(priority['header'])==priority['header_sha256'] and headers['local']==priority['baseline_header_sha256'];assert priority['probe_sha256']==sha(d/'probe.cpp');headers['local-priority']=sha(priority['header'])
qrecipe=json.loads((d/'priority-qualification-ready.json').read_text());assert sha(qrecipe['source'])==qrecipe['source_sha256'] and qrecipe['priority_header_sha256']==headers['local-priority']
roles=[('baseline','baseline','steal'),('first-steal','first-steal','steal')]+[('local-'+mode,'local',mode) for mode in ['round-robin','steal']]+[('local-priority-steal','local-priority','steal')]
def run(cmd,where,env,timeout=45):
 where.mkdir(parents=True,exist_ok=True);start=time.monotonic();timedout=False
 with (where/'stdout.log').open('w') as log:
  child=subprocess.Popen(cmd,cwd=workspace,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
  while True:
   pid,status,usage=os.wait4(child.pid,os.WNOHANG)
   if pid:child.returncode=os.waitstatus_to_exitcode(status);break
   if time.monotonic()-start>timeout:
    kill_owned_tree(child.pid,where/'timeout-cleanup.json');_,status,usage=os.wait4(child.pid,0);child.returncode=os.waitstatus_to_exitcode(status);timedout=True;break
   time.sleep(.01)
 r={'command':cmd,'returncode':child.returncode,'status':'timeout' if timedout else 'completed' if child.returncode==0 else 'failed','process_wall_seconds':time.monotonic()-start,'user_cpu_seconds':usage.ru_utime,'system_cpu_seconds':usage.ru_stime,'total_cpu_seconds':usage.ru_utime+usage.ru_stime,'peak_rss_kib':usage.ru_maxrss,'voluntary_context_switches':usage.ru_nvcsw,'involuntary_context_switches':usage.ru_nivcsw,'harness_sha256':sha(d/'probe.cpp'),'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()};save(where/'receipt.json',r);assert not timedout and not child.returncode,(where,r);return r
cleanenv={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')}
if a.phase=='build':
 binaries={}
 commands=dict(manifest['build_commands']);commands['local-priority']=shlex.join(priority['compile_command'])
 for label,text in commands.items():
  cmd=shlex.split(text);run(cmd,d/'build'/label,cleanenv,300);binary=d/('probe-'+label);binaries[label]={'binary':str(binary),'sha256':sha(binary),'header_sha256':headers[label]}
 auxiliary={}
 for name,command in [('priority-queue',qrecipe['compile_command']),('priority-races',qrecipe['optional_priority_races_compile_command'])]:
  run(command,d/'build'/name,cleanenv,300);output=Path(command[command.index('-o')+1]);auxiliary[name]={'binary':str(output),'sha256':sha(output),'header_sha256':headers['local-priority']}
 save(d/'build-complete.json',{'state':'compiled','harness_sha256':sha(d/'probe.cpp'),'binaries':binaries,'auxiliary':auxiliary,'qualification_source_sha256':qrecipe['source_sha256'],'races_source_sha256':sha(p/'standalone-races/races.cpp'),'wrapper_sha256':sha(Path(__file__))});raise SystemExit()
build=json.loads((d/'build-complete.json').read_text());assert build['harness_sha256']==sha(d/'probe.cpp')
for label,b in build['binaries'].items():assert sha(b['binary'])==b['sha256'] and headers[label]==b['header_sha256']
for b in build['auxiliary'].values():assert sha(b['binary'])==b['sha256'] and headers['local-priority']==b['header_sha256']
assert build['qualification_source_sha256']==qrecipe['source_sha256'];assert build['races_source_sha256']==sha(p/'standalone-races/races.cpp')
if a.phase=='qualify':
 # One-worker queue ordering diagnostic only; affine is not engine/policy qualified.
 for policy in ['round-robin','steal','lane-affine']:
  where=d/'qualify'/('priority-queue-'+policy);assert not (where/'receipt.json').exists();r=run([build['auxiliary']['priority-queue']['binary']],where,dict(cleanenv,GLOB2_DEFERRED_DISPATCH=policy));assert 'PRIORITY_QUEUE_PASS' in (where/'stdout.log').read_text()
 where=d/'qualify'/'priority-races';assert not (where/'receipt.json').exists();r=run([build['auxiliary']['priority-races']['binary'],'all'],where,dict(cleanenv,GLOB2_DEFERRED_DISPATCH='steal'),60);assert 'PASS all' in (where/'stdout.log').read_text()
if a.phase=='timing':assert json.loads((d/'qualify-complete.json').read_text())['build']==build
rows=[];digests={};repeats=1 if a.phase=='qualify' else 4;allocations={'3physical6SMT':[12,13,14,28,29,30]} if a.phase=='qualify' else {'3physical6SMT':[12,13,14,28,29,30],'all32':list(range(32))}
for allocation,cpus in allocations.items():
 for kind in ['required','optional']:
  for repeat in range(repeats):
   order=roles[repeat%len(roles):]+roles[:repeat%len(roles)]
   if repeat%2:order=list(reversed(order))
   for role,label,policy in order:
    where=d/a.phase/(allocation+'-'+kind+'-'+role+'-r'+str(repeat));assert not (where/'receipt.json').exists(),'Never silently reuse latency receipt'
    binary=build['binaries'][label];env=dict(cleanenv,GLOB2_DEFERRED_DISPATCH=policy);cmd=['taskset','-c',','.join(map(str,cpus)),binary['binary'],'trace' if a.phase=='qualify' else 'off',kind];r=run(cmd,where,env);r.update(role=role,policy=policy,kind=kind,repeat=repeat,allocation=allocation,binary_sha256=binary['sha256'],header_sha256=headers[label]);r['validation_errors']=[]
    try:
     entries=[json.loads(line.split(' ',1)[1]) for line in (where/'stdout.log').read_text().splitlines() if line.startswith('LATENCY_RESULT ')];assert len(entries)==1;n=entries[0];assert n['later_total']==128 and n['result_bytes']==129*8 and 0<=n['later_started_before_urgent']<=128 and n['urgent_join_ns']>0;assert n['trace']==(a.phase=='qualify');assert kind!='optional' or n['presentation_start_delay_ns']>=0
     assert digests.setdefault(kind,n['digest'])==n['digest'],'Payload digest differs';r['latency']=n
    except (AssertionError,KeyError,ValueError) as error:r['validation_errors'].append(str(error))
    save(where/'receipt.json',r);assert not r['validation_errors'],r;rows.append(r)
    save(d/'progress.json',{'phase':a.phase,'completed':len(rows),'last_role':role,'kind':kind})
save(d/(a.phase+'-complete.json'),{'state':'payload_verified','priority_acceptance':'Requires explicit review of urgent and presentation latency versus baseline; payload correctness alone is insufficient.','cases':len(rows),'build':build,'payload_digests':digests})
if a.phase=='timing':
 comparisons=[]
 for allocation in allocations:
  for kind in ['required','optional']:
   cells={role:[r for r in rows if r['role']==role and r['allocation']==allocation and r['kind']==kind] for role,_,_ in roles}
   for role,rr in cells.items():
    if role=='baseline':continue
    metrics={}
    for metric in ['urgent_join_ns','later_started_before_urgent','presentation_start_delay_ns']:
     before=statistics.median(r['latency'][metric] for r in cells['baseline']);after=statistics.median(r['latency'][metric] for r in rr);metrics[metric]={'baseline_median':before,'candidate_median':after,'ratio':after/before if before>0 else None,'candidate_samples':[r['latency'][metric] for r in rr],'baseline_samples':[r['latency'][metric] for r in cells['baseline']]}
    comparisons.append({'allocation':allocation,'kind':kind,'candidate':role,'metrics':metrics})
 save(d/'analysis.json',{'scope':'Conditional gated CPU backlog latency, four repeats of one fixed workload; no engine frame/input population claim. Process CPU includes payload validation after inner urgent wait; Off only timings.','comparisons':comparisons,'all_payloads_verified':True,'cases':len(rows)})
print('Verified latency',a.phase,len(rows),'cases')
