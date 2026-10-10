import os,json,time,struct,hashlib,subprocess,sys
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'test'));from benchmark_gpu_offload import resource_snapshot
candidate=root/'artifacts/gpu-offload/candidates/786feaa6a';receipt=json.loads((candidate/'build-receipt.json').read_text())
output=root/'artifacts/gpu-offload/candidate-786feaa6a-render-warm-role-preflight';output.mkdir(exist_ok=False)
binary=candidate/'glob2';fixture=root/'artifacts/gpu-offload/fixtures/512-open/initial.game.gz'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sha(binary)==receipt['binary_sha256'];assert os.environ.get('DISPLAY')
assert sha(fixture)=='397bec44126f4611773829fd9093bf8d66bbf1b0c7410c9302baf5945ee522f7'
renderer_keys=('SDL_VIDEO_DRIVER','SDL_VIDEODRIVER','SDL_RENDER_DRIVER','__GLX_VENDOR_LIBRARY_NAME','__EGL_VENDOR_LIBRARY_FILENAMES','__NV_PRIME_RENDER_OFFLOAD','__NV_PRIME_RENDER_OFFLOAD_PROVIDER','DRI_PRIME','MESA_LOADER_DRIVER_OVERRIDE','LIBGL_ALWAYS_SOFTWARE','GALLIUM_DRIVER','LP_NUM_THREADS','MESA_GL_VERSION_OVERRIDE','EGL_PLATFORM')
def trace(path):
 data=path.read_bytes();assert data[:4]==b'GCS1';teams,players,count,flags=struct.unpack_from('<4I',data,4);assert teams in range(1,33) and count==768;pos=20;ticks=[]
 def u32():
  nonlocal pos
  assert pos+4<=len(data);v=struct.unpack_from('<I',data,pos)[0];pos+=4;return v
 for _ in range(count):
  ticks.append(u32());u32()
  for team in range(teams):
   u32()
   for category in range(2):
    objects=u32();assert objects<=65536
    for _ in range(objects):
     assert pos+2<=len(data);pos+=2;u32();n=u32();assert n<=1048576;pos+=4*n;assert pos<=len(data)
 assert ticks==list(range(768)) and pos==len(data)
 return dict(sha256=sha(path),first_tick=ticks[0],last_tick=ticks[-1],ticks=count,closed_body=True)
rows=[]
for driver,backend in [('x11','cpu'),('x11','opencl'),('default','cpu'),('default','opencl')]:
 ident=backend+'-'+driver;d=output/ident;d.mkdir();profile=d/'profile';profile.mkdir();replay=d/'game.replay';diagnostic=d/'rendered-cpu.json'
 env=os.environ.copy();env.pop('SDL_VIDEO_DRIVER',None)
 if driver=='x11':env['SDL_VIDEO_DRIVER']='x11'
 env.update({'GLOB2_USER_DATA_DIR':str(profile),'GLOB2_REPLAY_PATH':str(replay),'GLOB2_CHECKSUM_SIDECAR':'1','GLOB2_TEAM_TIMELINE':'1','GLOB2_PERF_DISTRIBUTIONS':'1','GLOB2_GRADIENT_BACKEND':backend,'GLOB2_GRADIENT_PLAN':'frozen8','GLOB2_OPENCL_DEVICE':'0','GLOB2_OPENCL_CHECK_INTERVAL':'8','GLOB2_OPENCL_POLL_US':'0','GLOB2_OPENCL_PROFILE':'0','GLOB2_GRADIENT_TUNING':'0','GLOB2_GRADIENT_BATCH':'1','GLOB2_GRADIENT_WORKER_NOOP':'0','GLOB2_OPENCL_ACTIVE_EPOCH':'0','GLOB2_OPENCL_PARITY_BOUND':'0','GLOB2_OPENCL_DIRECT_SEED_UPLOAD':'0','GLOB2_OPENCL_API_CPU':'0','GLOB2_OPENCL_UNIFORM_METADATA':'0','GLOB2_GRADIENT_CPU_ENVELOPE':'0','GLOB2_AI_SCHEDULER_DIAGNOSTICS':'1','GLOB2_GRADIENT_DIAGNOSTICS':'1','GLOB2_RENDERED_CPU_DIAGNOSTICS_PATH':str(diagnostic),'GLOB2_RENDERED_CPU_DIAGNOSTICS_WARMUP_TICKS':'256','GLOB2_RENDERED_CPU_DIAGNOSTICS_MEASURE_TICKS':'512'})
 command=[str(binary),'game','repeat',str(fixture),'--ticks','768','--runs','1','--compute-threads','8','--renderer','gpu','--window-size','1280x720','--no-fullscreen','--no-custom-cursor','--mute','--display']
 before=resource_snapshot(gpu_uuid='GPU-289a3035-0b2c-d273-67db-ccc36b6ca73b');snapshots=[];began=time.monotonic_ns()
 with (d/'engine.log').open('w') as log:
  process=subprocess.Popen(command,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT);next_sample=time.monotonic();deadline=next_sample+240
  while True:
   pid,status,usage=os.wait4(process.pid,os.WNOHANG)
   if pid:process.returncode=os.waitstatus_to_exitcode(status);break
   if time.monotonic()>deadline:process.kill();pid,status,usage=os.wait4(process.pid,0);process.returncode=os.waitstatus_to_exitcode(status);break
   if time.monotonic()>=next_sample:
    tasks=[]
    for task in (Path('/proc')/str(process.pid)/'task').glob('*'):
     try:
      stat=(task/'stat').read_text();fields=stat[stat.rindex(')')+2:].split();tasks.append({'tid':int(task.name),'comm':(task/'comm').read_text().strip(),'cpu_clock_ticks':int(fields[11])+int(fields[12]),'clock_ticks_per_second':os.sysconf('SC_CLK_TCK'),'allowed':next(line for line in (task/'status').read_text().splitlines() if line.startswith('Cpus_allowed_list:'))})
     except OSError:pass
    inventory=subprocess.run(['nvidia-smi'],capture_output=True,text=True,timeout=10);snapshots.append({'monotonic_ns':time.monotonic_ns(),'tasks':tasks,'nvidia_smi':inventory.stdout,'nvidia_smi_status':inventory.returncode});next_sample=time.monotonic()+2
   time.sleep(.1)
 r={'variant':ident,'command':command,'environment_overrides':{k:v for k,v in env.items() if k.startswith('GLOB2_')},'renderer_selection_environment':{k:env.get(k) for k in renderer_keys},'native_pid':process.pid,'exit_code':process.returncode,'binary_sha256':sha(binary),'fixture_sha256':sha(fixture),'snapshots':snapshots,'before':before,'after':resource_snapshot(gpu_uuid='GPU-289a3035-0b2c-d273-67db-ccc36b6ca73b'),'whole_session_process_cpu_ns':round((usage.ru_utime+usage.ru_stime)*1e9),'whole_session_wall_ns':time.monotonic_ns()-began,'qualifying_evidence':False,'stage':'rendered_role_diagnose','window_overhead':'checksum sidecar, diagnostic stage clocks and process inventory are enabled. No performance admission.'}
 (d/'receipt.json').write_text(json.dumps(r,indent=2)+'\n');assert process.returncode==0
 report=json.loads(diagnostic.read_text());assert report['schema']=='glob2-rendered-cpu-diagnostics-v1' and all(report[k] for k in ('process_cpu_valid','owner_cpu_valid','wall_valid'))
 assert report['start']['tick']==256 and report['end']['tick']==768 and report['final_tick']==768
 assert report['start']['owner_tid']==report['end']['owner_tid'] and not report['missed_boundary']
 start,end=report['start']['counters'],report['end']['counters'];r['warm_deltas']={k:end[k]-v for k,v in start.items() if type(v)==int};r['rendered_report']=report;r['trace']=trace(Path(str(replay)+'.checksums'))
 if backend=='opencl':assert r['warm_deltas']['gpu_complete_fields']>0
 roles=report['post_stop_gradient_totals']['counters'];r['roles']={'presentation_main_tid':process.pid,'simulation_owner_tid':report['end']['owner_tid'],'coordinator_tid':roles.get('coordinator_tid'),'compute_config_caller_tid':roles.get('compute_config_owner_tid'),'compute_worker_tids':{k:v for k,v in roles.items() if k.startswith('compute_worker_tid_')}}
 (d/'receipt.json').write_text(json.dumps(r,indent=2)+'\n');rows.append(r);print(ident,report['process_cpu_delta_ns'],report['rendering_identity'],r['roles'],flush=True)
assert len({r['trace']['sha256'] for r in rows})==1
(output/'summary.json').write_text(json.dumps({'valid':True,'rows':rows,'qualifying_evidence':False,'CPU_budget':'nine physical cores for role preflight only; timed rendered budget determined after observed role identity'},indent=2)+'\n')
