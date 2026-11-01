"""Run two selected font probes and six current registered UI/game captures."""
from pathlib import Path
import gzip,hashlib,json,os,shutil,signal,subprocess,time
ROOT=Path(__file__).resolve().parents[4];BASE=Path(__file__).resolve().parent;OUT=BASE/'render-refined'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert (BASE/'capture-variant-provenance.json').exists()
assert not (OUT/'runner.json').exists(),'Use a separate explicit attempt; never infer completion from PNGs'
OUT.mkdir(parents=True,exist_ok=True)
record={'git_revision':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'actual_binaries':{},'jobs':[],'cap_bytes':1536*1024**2,'minimum_free_bytes':2*1024**3,'limits':['New111 selected online values only in font matrix; old75288 retained separately with exact code/target/font carry.','All menu/game jobs use normal registered resources, zero overlays.','Actual loaded-game history; no invented readings or completed-match/tutorial claim.','Compact desktop presentation is not an Android/iOS runtime claim.']}
def write(): (OUT/'runner.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
for key in ('official','variant'):
 p=BASE/(key+'-capture-stripped');assert p.is_file();record['actual_binaries'][key]={'path':str(p),'sha256':sha(p)}
write()
env=dict(os.environ,SDL_VIDEO_DRIVER='x11',SDL_VIDEODRIVER='x11',SDL_AUDIODRIVER='dummy',XDG_SESSION_TYPE='x11',LIBGL_ALWAYS_SOFTWARE='1')
env['LD_LIBRARY_PATH']=str(ROOT/'artifacts/marathi-localization/native-deps/lib')+':'+env.get('LD_LIBRARY_PATH','')
for x in ('DISPLAY','WAYLAND_DISPLAY'):env.pop(x,None)
for w,h in [(1280,900),(390,844)]:
 for name,kind,producer in ([('main','main','variant'),('settings-player','settings-player','variant')] if w==390 else [])+[('gameplay','gameplay','variant')]:
  dest=OUT/f'{w}x{h}'/name;dest.mkdir(parents=True,exist_ok=True)
  profile=OUT/'profiles'/f'{w}x{h}'/name
  taskenv=dict(env,GLOB2_USER_DATA_DIR=str(profile),GLOB2_MOBILE_UI='touch-auto' if w==390 else '0',XDG_DATA_HOME=str(profile/'data'),XDG_CONFIG_HOME=str(profile/'config'),XDG_CACHE_HOME=str(profile/'cache'))
  pilot=BASE/'preview-inputs/previews'/('new111' if name=='matrix' else 'full')
  if producer=='variant': taskenv['GLOB2_CAPTURE_REGISTERED']='1'
  binary=BASE/(producer+'-capture-stripped');assert sha(binary)==record['actual_binaries'][producer]['sha256']
  mode='matrix' if name=='matrix' else 'game' if name=='gameplay' else 'screen'
  cmd=['xvfb-run','-a','-s','-screen 0 1600x1200x24 -noreset','python3','test/xvfb_session.py',str(binary),mode,str(ROOT),str(pilot),str(dest),str(w),str(h)]+([kind] if kind else [])
  deadline=900 if mode=='matrix' else 600 if mode=='game' else 300
  start=time.monotonic();log=dest/'capture.log.gz'
  p=subprocess.Popen(cmd,cwd=ROOT,env=taskenv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
  import threading
  def capture():
   with gzip.open(log,'wb') as f:
    while chunk:=p.stdout.read1(65536):f.write(chunk);f.flush()
  t=threading.Thread(target=capture);t.start();failure=None
  try:
   while p.poll() is None:
    if time.monotonic()-start>deadline:raise TimeoutError('Bounded validation deadline, not a performance claim')
    if sum(x.stat().st_size for x in OUT.rglob('*') if x.is_file())>record['cap_bytes']:raise RuntimeError('Evidence cap exceeded')
    if shutil.disk_usage(OUT).free<record['minimum_free_bytes']:raise RuntimeError('Free-space guard exceeded')
    time.sleep(2)
  except BaseException as e:
   failure=str(e);os.killpg(p.pid,signal.SIGTERM)
   try:p.wait(timeout=10)
   except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait()
  finally:
   t.join(timeout=10)
   job={'viewport':[w,h],'name':name,'producer':producer,'command':cmd,'output':str(dest.relative_to(OUT)),'exit_code':p.returncode,'elapsed_seconds':time.monotonic()-start,'deadline_seconds':deadline,'failure':failure,'log_sha256':sha(log)}
   record['jobs'].append(job);write();print(job,flush=True)
  assert p.returncode==0 and failure is None,'Preserve failed attempt, do not mark completion'
  report=dest/('font-matrix.json' if mode=='matrix' else name+'.json')
  assert report.exists(),str(report)
  value=json.loads(report.read_text(encoding='utf-8'))
  if name=='matrix': assert len(value['samples'])==666
  else:
   identity=value['evidence_identity'];probe=identity['registered_resource_probe'];assert probe['complete'] and probe['native_name']=='मराठी' and probe['registered_language_count']==39
   assert value['native_keys_overridden']==0
record['complete']=True;write()
