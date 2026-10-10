import os,json,time,struct,hashlib,subprocess,sys
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'test'));from benchmark_gpu_offload import resource_snapshot
output=root/'artifacts/gpu-offload/candidate-93f76f134-render-x11-preflight';output.mkdir(exist_ok=False)
binary=root/'artifacts/gpu-offload/candidates/93f76f134/glob2';fixture=root/'artifacts/gpu-offload/fixtures/512-open/initial.game.gz'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert sha(binary)=='935de76a54c72051ccd7c854482f08d38b690a91c217b561c7a3d5cd036a5e42'
assert os.environ.get('DISPLAY'), 'actual desktop DISPLAY required'
def validate_sidecar(path):
 data=path.read_bytes();assert data[:4]==b'GCS1';teams,players,count,flags=struct.unpack_from('<4I',data,4);assert teams in range(1,33) and count==256;pos=20;ticks=[]
 def u32():
  nonlocal pos
  assert pos+4<=len(data);value=struct.unpack_from('<I',data,pos)[0];pos+=4;return value
 for _ in range(count):
  ticks.append(u32());u32()
  for team in range(teams):
   u32()
   for category in range(2):
    objects=u32();assert objects<=65536
    for _ in range(objects):
     assert pos+2<=len(data);pos+=2;u32();length=u32();assert length<=1048576;pos+=4*length;assert pos<=len(data)
 assert ticks==list(range(256)) and pos==len(data)
 return {'sha256':sha(path),'first_tick':ticks[0],'last_tick':ticks[-1],'ticks':count,'complete_body':True,'teams':teams,'flags':flags}
rows=[]
for ident,backend,display in [('cpu-displayed-x11','cpu',True),('gpu-displayed-x11','opencl',True)]:
 d=output/ident;d.mkdir();profile=d/'profile';profile.mkdir();replay=d/'game.replay'
 env=os.environ|{'SDL_VIDEO_DRIVER':'x11','GLOB2_USER_DATA_DIR':str(profile),'GLOB2_REPLAY_PATH':str(replay),'GLOB2_CHECKSUM_SIDECAR':'1','GLOB2_TEAM_TIMELINE':'1','GLOB2_PERF_DISTRIBUTIONS':'1','GLOB2_GRADIENT_BACKEND':backend,'GLOB2_GRADIENT_PLAN':'frozen8','GLOB2_OPENCL_DEVICE':'0','GLOB2_OPENCL_CHECK_INTERVAL':'8','GLOB2_OPENCL_POLL_US':'0','GLOB2_OPENCL_PROFILE':'0','GLOB2_GRADIENT_TUNING':'0','GLOB2_GRADIENT_BATCH':'1','GLOB2_GRADIENT_WORKER_NOOP':'0','GLOB2_OPENCL_ACTIVE_EPOCH':'0','GLOB2_OPENCL_PARITY_BOUND':'0','GLOB2_OPENCL_DIRECT_SEED_UPLOAD':'0','GLOB2_AI_SCHEDULER_DIAGNOSTICS':'0','GLOB2_GRADIENT_DIAGNOSTICS':'0'}
 command=[str(binary),'game','repeat',str(fixture),'--ticks','256','--runs','1','--compute-threads','8','--renderer','gpu','--window-size','1280x720','--no-fullscreen','--no-custom-cursor','--mute'];command+=['--display'] if display else []
 snapshots=[];started=time.monotonic_ns();before=resource_snapshot(gpu_uuid='GPU-289a3035-0b2c-d273-67db-ccc36b6ca73b')
 with (d/'engine.log').open('w') as log:
  process=subprocess.Popen(command,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT);next_sample=time.monotonic();deadline=next_sample+180
  while True:
   pid,status,usage=os.wait4(process.pid,os.WNOHANG)
   if pid:process.returncode=os.waitstatus_to_exitcode(status);break
   if time.monotonic()>deadline:process.kill();pid,status,usage=os.wait4(process.pid,0);process.returncode=os.waitstatus_to_exitcode(status);break
   if time.monotonic()>=next_sample:
    tasks=[]
    for task in (Path('/proc')/str(process.pid)/'task').glob('*'):
     try:tasks.append({'tid':int(task.name),'comm':(task/'comm').read_text().strip(),'allowed':next(line for line in (task/'status').read_text().splitlines() if line.startswith('Cpus_allowed_list:'))})
     except OSError:pass
    inventory=subprocess.run(['nvidia-smi'],capture_output=True,text=True,timeout=10);snapshots.append({'monotonic_ns':time.monotonic_ns(),'pid':process.pid,'tasks':tasks,'nvidia_smi':inventory.stdout,'nvidia_smi_status':inventory.returncode});next_sample=time.monotonic()+2
   time.sleep(.1)
 receipt={'variant':ident,'command':command,'environment_overrides':{k:v for k,v in env.items() if k.startswith('GLOB2_')},'renderer_selection_environment':{k:env.get(k) for k in ('SDL_VIDEO_DRIVER', 'SDL_VIDEODRIVER', 'SDL_RENDER_DRIVER', '__GLX_VENDOR_LIBRARY_NAME', '__EGL_VENDOR_LIBRARY_FILENAMES', '__NV_PRIME_RENDER_OFFLOAD', '__NV_PRIME_RENDER_OFFLOAD_PROVIDER', 'DRI_PRIME', 'MESA_LOADER_DRIVER_OVERRIDE', 'LIBGL_ALWAYS_SOFTWARE', 'GALLIUM_DRIVER', 'LP_NUM_THREADS', 'MESA_GL_VERSION_OVERRIDE', 'EGL_PLATFORM')},'native_pid':process.pid,'exit_code':process.returncode,'whole_session_process_cpu_ns':round((usage.ru_utime+usage.ru_stime)*1e9),'whole_session_wall_ns':time.monotonic_ns()-started,'binary_sha256':sha(binary),'fixture_sha256':sha(fixture),'before':before,'after':resource_snapshot(gpu_uuid='GPU-289a3035-0b2c-d273-67db-ccc36b6ca73b'),'snapshots':snapshots,'actual_desktop':True,'CPU_window':'whole_session_only','histogram_scope':'collector intervals may include startup/outsidewarm; not warm p99','qualification':False}
 (d/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');assert process.returncode==0
 receipt['trace']=validate_sidecar(Path(str(replay)+'.checksums'));rows.append(receipt);print(ident,receipt['trace'],flush=True)
assert len({r['trace']['sha256'] for r in rows})==1 and rows[0]['trace']['sha256']=='7491e47de87df31742a76b2bd532d5668047d2551e5541f1410cf8e9458485ac'
(output/'summary.json').write_text(json.dumps({'valid':True,'normal_loop_exact':True,'rows':rows,'qualification':False},indent=2)+'\n')
