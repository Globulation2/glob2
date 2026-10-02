import subprocess,os,json,time,signal,pathlib,shutil
root=pathlib.Path(__file__).resolve().parents[2]
out=root/'artifacts/memory-optimization/profile-final';out.mkdir(exist_ok=True)
cmd=[str(root/'build/darwin/client/release/src/glob2'),'--run-game','--load-game',str(root/'artifacts/memory-profile/game/initial.game.gz'),'--ticks','45512','--save','every:15000','--telemetry','team-timeline','--output-dir',str(out/'game')]
env=os.environ.copy();env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',MallocStackLogging='1')
(out/'commands.json').write_text(json.dumps({'game':cmd},indent=2))
with (out/'game.log').open('w') as log:
 p=subprocess.Popen(cmd,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT)
 (out/'pid').write_text(str(p.pid));captured=set();start=time.time()
 while p.poll() is None:
  progress=out/'game/progress.jsonl';tick=0
  if progress.exists():
   lines=progress.read_text().splitlines()
   if lines:tick=json.loads(lines[-1])['tick']
  for target in (256,15000,30000,45000):
   if tick>=target and target not in captured:
    os.kill(p.pid,signal.SIGSTOP);paused=time.time()
    try:
     for name,args in [('heap',['heap','-s','--showSizes']),('vmmap',['vmmap','-summary'])]:
      with (out/f'{target}-{name}.txt').open('w') as f:subprocess.run(args+[str(p.pid)],stdout=f,stderr=subprocess.STDOUT,timeout=45,check=True)
     (out/f'{target}-capture.json').write_text(json.dumps({'observed_tick_lower_bound':tick,'target':target,'pause_seconds':time.time()-paused}))
     captured.add(target);print('Captured',target,'at',tick,flush=True)
    finally:os.kill(p.pid,signal.SIGCONT)
  time.sleep(.15)
 if p.returncode:raise RuntimeError(f'Game exit {p.returncode}')
 assert len(captured)==4,captured
for name in ('summarize_heap.py','classify_heap.py'):shutil.copy(root/'artifacts/memory-profile'/name,out/name)
subprocess.run(['python3',str(out/'summarize_heap.py')],check=True)
subprocess.run(['python3',str(out/'classify_heap.py')],check=True)
