import json,os,subprocess,time,shutil,hashlib,re
from pathlib import Path
ROOT=Path.cwd();OPT=ROOT/'artifacts/serial-opt';OUT=ROOT/'artifacts/serial-reanalysis';TREE=Path('/home/bradley/.codex/worktrees/serial-opt-gcc13/glob2');BUILD=TREE/'build/serial-opt-gcc13'
REV='6f8cf442fe5e35aa7e7773f51f3854fa3c8e0ccf'
while not (OPT/'release-verification.exit').exists():time.sleep(5)
assert (OPT/'release-verification.exit').read_text().strip()=='0','release verification failed'
owned=['SConstruct','src/app/cli/Headless.cpp']
changed=set(subprocess.check_output(['git','diff','--name-only'],cwd=TREE,text=True).splitlines());assert changed<=set(owned),changed
subprocess.run(['git','restore','--source=HEAD','--',*owned],cwd=TREE,check=True)
subprocess.run(['git','switch','--detach',REV],cwd=TREE,check=True)
subprocess.run(['git','apply',str(ROOT/'artifacts/serial-audit/instrumentation.patch')],cwd=TREE,check=True)
cmd=json.loads((OPT/'gcc13-build-command.json').read_text())[:-1]
(OUT/'build-command.json').write_text(json.dumps(cmd,indent=2))
with (OUT/'build.log').open('w') as f:subprocess.run(cmd,cwd=TREE,env=os.environ|{'GLOB2_SDL3_PREFIX':str(ROOT/'build/serial-audit-deps/prefix')},stdout=f,stderr=subprocess.STDOUT,check=True)
binary=OUT/'glob2';shutil.copy2(BUILD/'src/glob2',binary)
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
(OUT/'metadata.json').write_text(json.dumps({'revision':REV,'compiler':subprocess.check_output(['g++-13','--version'],text=True),'binary_sha256':sha(binary),'build_command':cmd,'instrumentation_patch_sha256':sha(ROOT/'artifacts/serial-audit/instrumentation.patch')},indent=2))
with (OUT/'elf-sections.txt').open('w') as f:subprocess.run(['readelf','-SW',str(binary)],stdout=f,check=True)
assert '.debug_info' in (OUT/'elf-sections.txt').read_text()
scenarios=json.loads((ROOT/'artifacts/serial-audit/fixtures.json').read_text())['scenarios']
# Prioritize normal shared execution; then serial and automatic sizing controls.
results=[]
for n in [4,1,32]:
 for s in [s for s in scenarios if n==4 or s['id'] in ('sparse','dense')]:
  for mode in (['perf','counters','timing-0','timing-1','timing-2'] if s['id'] in ('sparse','dense') else ['perf','counters','timing-0']):
   p=OUT/f"{s['id']}-{n}-{mode}";p.mkdir(exist_ok=True)
   args=[str(binary),'--run-game','--load-game',s['fixture'],'--ticks',str(s['tick']+4096),'--compute-threads',str(n),'--benchmark-warmup','0','--output-dir',str(p),'--profile','serial-reanalysis']
   env=os.environ|{'GLOB2_USER_DATA_DIR':str(p/'userdata')};prefix=[]
   if mode in ['perf','counters']:
    control=p/'control';ack=p/'ack'
    for f in [control,ack]:
     if f.exists():f.unlink()
     os.mkfifo(f,0o666);f.chmod(0o666)
    prefix=['sudo','-n','perf','record' if mode=='perf' else 'stat','--delay=-1',f'--control=fifo:{control},{ack}']
    if mode=='perf':prefix+=['-F','499','-e','cycles','--call-graph','dwarf,16384','-o',str(p/'perf.data')]
    else:prefix+=['-e','task-clock,context-switches,cpu-migrations,cycles,instructions,branches,branch-misses,cache-misses','-x',',','-o',str(p/'counters.csv')]
    prefix+=['--','runuser','-u','bradley','--','env',f'GLOB2_AUDIT_CONTROL={control}',f'GLOB2_AUDIT_ACK={ack}',f"GLOB2_USER_DATA_DIR={p/'userdata'}"]
   command=prefix+args;(p/'command.json').write_text(json.dumps(command,indent=2))
   # Leave unrelated work alone; wait for active compilers to clear.
   import psutil
   active_compilers=sum(t.info['name'] in ('cc1plus','cc1','wasm-opt','lto1') and t.info['status']!=psutil.STATUS_STOPPED for t in psutil.process_iter(['name','status']))
   (p/'host-load.json').write_text(json.dumps({'unix_time':time.time(),'load_average':os.getloadavg(),'cpu_percent':psutil.cpu_percent(interval=0.1,percpu=True),'active_compilers':active_compilers},indent=2))
   with (p/'engine.log').open('w') as f:subprocess.run(command,cwd=TREE,env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=600)
   if mode in ['perf','counters']:
    subprocess.run(['sudo','-n','chown','-R',f'{os.getuid()}:{os.getgid()}',str(p)],check=True);control.unlink();ack.unlink()
   result=json.loads((p/'result.json').read_text());ref=json.loads((ROOT/f"artifacts/serial-audit/profiles/{s['id']}-1-verify/result.json").read_text())
   for key in ['initialChecksum','finalChecksum','ticks','gradient_delay','growth_delay']:assert result[key]==ref[key],(p,key)
   log=(p/'engine.log').read_text();m=re.search(r'owner_tid=(\d+) owner_cpu_ns=(\d+) process_cpu_ns=(\d+) wall_ns=(\d+)',log);join=re.search(r'join_wait_ns=(\d+)',log)
   results.append({'fixture':s['id'],'participants':n,'mode':mode,'owner_tid':int(m[1]),'owner_cpu_ns':int(m[2]),'process_cpu_ns':int(m[3]),'wall_ns':int(m[4]),'join_wait_ns':int(join[1]),'final_checksum':result['finalChecksum']})
   (OUT/'results.json').write_text(json.dumps(results,indent=2))
   if mode=='perf':
    for name,extra in [('report.txt',['--no-children','--call-graph','none']),('owner-callgraph.txt',['--tid',m[1],'--children','--call-graph','graph,0.5,caller'])]:
     with (p/name).open('w') as f:subprocess.run(['perf','report','--stdio','--sort','pid,symbol',*extra,'-i',str(p/'perf.data')],stdout=f,stderr=subprocess.DEVNULL,check=True)
   print('PASS',s['id'],n,mode,flush=True)
subprocess.run(['git','restore','--source=HEAD','--',*owned],cwd=TREE,check=True)
print('Merged-revision profiling complete',flush=True)
