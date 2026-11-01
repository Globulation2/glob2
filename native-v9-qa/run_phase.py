"""Final-v9 QA runner. Preparation only until final clean source and binary handoff.
Usage: python3 .../run_phase.py fonts|surfaces|engine|unit BINARY --handoff REVISION
API503 runner is separate api_notices.py with the same explicit handoff guard.
"""
from pathlib import Path
import sys,json,subprocess,os,time,hashlib,fnmatch,gzip,shutil,signal
R=Path(__file__).resolve().parents[3];B=R/'artifacts/bulgarian';OUT=B/'native-captures/v9-final'
def sha(f):return hashlib.sha256(f.read_bytes()).hexdigest()
def write(p,d):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(d,ensure_ascii=False,indent=2)+'\n')
def source_guard(revision):
 frozen=json.loads((B/'v9-frozen-inputs.json').read_text());assert frozen['compiled_revision']==revision,'Actual compiled handoff identity mismatch'
 assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=R,text=True).strip()==frozen['final_source_revision'],'Final source HEAD changed'
 assert not subprocess.check_output(['git','status','--porcelain'],cwd=R,text=True).strip(),'Final source must be clean'
 patch=subprocess.check_output(['git','diff','--binary','--full-index',revision,frozen['final_source_revision']],cwd=R);assert hashlib.sha256(patch).hexdigest()==frozen['source_closure']['exact_patch_sha256'],'Final-source closure changed'
 for name,expected in frozen['input_files_sha256'].items():assert sha(R/name)==expected,('Frozen input changed',name)
 return frozen

def guard(binary,revision):
 frozen=source_guard(revision)
 assert binary.is_file()and revision.encode()in binary.read_bytes(),'Binary lacks actual compiled revision'
 receipt=json.loads((B/'native-v9-qa/binary-handoff.json').read_text());assert receipt['revision']==revision and receipt['binaries'][binary.name]['sha256']==sha(binary),'Explicit final binary handoff hash required';return frozen

def environment(profile,phone=False,growth=100):
 user=profile/'user';user.mkdir(parents=True,exist_ok=True);(user/'preferences.txt').write_text(f'textSizePercent={growth}\nlanguage=bg\n')
 env=dict(os.environ,LD_LIBRARY_PATH=str(B/'sdl3/prefix/lib')+':'+os.environ.get('LD_LIBRARY_PATH',''),GLOB2_USER_DATA_DIR=str(user),XDG_DATA_HOME=str(profile/'data'),XDG_CONFIG_HOME=str(profile/'config'),XDG_CACHE_HOME=str(profile/'cache'),SDL_AUDIODRIVER='dummy',SDL_VIDEODRIVER='x11',GLOB2_RENDERER='software')
 env['XDG_SESSION_TYPE']='x11'
 for key in ['WAYLAND_DISPLAY','WAYLAND_SOCKET']:env.pop(key,None)
 env['SDL_VIDEO_DRIVER']='x11'
 if phone:env['GLOB2_MOBILE_UI']='1'
 else:env.pop('GLOB2_MOBILE_UI',None)
 return env

def run_font_bounded(cmd,env,log,receipt):
 limit=1073741824;peak=0;minimum_free=shutil.disk_usage(OUT).free;started=time.monotonic()
 def occupied():
  total=0
  for directory in OUT.glob('fonts*'):
   if not directory.is_dir():continue
   for root,dirs,files in os.walk(directory):
    for name in files:
     try:total+=(Path(root)/name).stat().st_size
     except FileNotFoundError:pass
  return total
 assert minimum_free>=2*limit,'Less than2GiB free before full-font execution'
 process=subprocess.Popen(cmd,cwd=R,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True);cause=None
 while process.poll()is None:
  size=occupied();peak=max(peak,size);minimum_free=min(minimum_free,shutil.disk_usage(OUT).free)
  if size>limit:cause='font-storage-budget-exceeded'
  elif minimum_free<limit:cause='free-space-reserve-exhausted'
  elif time.monotonic()-started>900:cause='timeout'
  if cause:
   os.killpg(process.pid,signal.SIGTERM)
   try:process.wait(timeout=10)
   except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
   break
  time.sleep(1)
 rc=cause if cause else process.returncode
 write(receipt,{'status':'Actual storage observation, never truncates a measurement into success','limit_bytes':limit,'peak_combined_font_output_bytes':peak,'minimum_free_bytes':minimum_free,'returncode':rc,'command':cmd,'seconds':round(time.monotonic()-started,2)})
 return rc

def run_capture(binary,phase,revision):
 frozen=guard(binary,revision);shard=os.environ.get('GLOB2_NATIVE_QA_SHARD','');shard_index=0;shards=1
 if shard:
  index,total=map(int,shard.split('/'));assert 1<=index<=total<=3,'At most three disjoint capture shards';shard_index=index-1;shards=total
 label=os.environ.get('GLOB2_NATIVE_QA_OUTPUT_LABEL','');assert not label or all(c.isalnum()or c in '-_'for c in label),'Invalid output label'
 base=OUT/(phase+('-'+label if label else'')+('-shard-'+str(shard_index+1)+'-of-'+str(shards)if shard else''));base.mkdir(parents=True,exist_ok=True);execution=base/'execution.json';records=[];jobs=[]
 if phase=='fonts':
  pilot=B/'v9-full-render-input/render-input/bg';delta={surf:json.loads((pilot/(surf+'.json')).read_text())for surf in ['native','online']};expected={(surf,k,branch)for surf,cat in delta.items()for k,v in cat.items()for branch in (v if isinstance(v,dict)else [''])};assert len(expected)==frozen['final_render_values']
  for view,w,h in [('desktop',1280,1024),('phone',480,960)]:
   for size in [10,13,20]:
    for scale in [1,2]:jobs.append((f'{view}-{size}x{scale}',view,w,h,100,'matrix',[str(size),str(scale)]))
 else:
  pilot=B/'v9-full-render-input/render-input/bg'
  # Eight jobs: four real script-editor three-state captures and four real statistics navigation captures.
  for view,w,h in [('desktop',1280,1024),('phone',390,844)]:
   for growth in [100,150]:
    jobs.append((f'{view}-{growth}-script-editor',view,w,h,growth,'script-editor',[]))
    jobs.append((f'{view}-{growth}-statistics',view,w,h,growth,'game',['statistics']))
  for view,w,h in [('desktop',1280,1024),('phone',390,844)]:
   for name in ['main','lobby','lobby-players','settings','settings-ais','settings-online','settings-player','new-map']:jobs.append((f'{view}-{name}',view,w,h,100,'screen',[name]))
   jobs.append((f'{view}-editor',view,w,h,100,'editor',[]))
   jobs.append((f'{view}-gameplay',view,w,h,100,'game',['gameplay']))
  # Actual compact west-access instruction remains contextual game text; briefing/state may differ.
  jobs.append(('phone-fortifications','phone',390,844,100,'game',['tutorial:fortifications']))
 # Optional two School briefings exercise changed wording where naturally visible; no lesson progress fabricated.
 if phase=='schools':
  pilot=B/'v9-full-render-input/render-input/bg';jobs=[(v+'-schools',v,w,h,100,'game',['tutorial:schools'])for v,w,h in [('desktop',1280,1024),('phone',390,844)]]
 all_jobs=len(jobs);jobs=[job for index,job in enumerate(jobs)if index%shards==shard_index];assert jobs
 identity={'output_label':label,'shard':shard or'1/1','total_planned_jobs':all_jobs,'assigned_jobs':len(jobs),'binary':str(binary),'binary_sha256':sha(binary),'revision':revision,'phase':phase,'scope':'Full final native/online catalog corpus; every literal/plural variant freshly measured in all12 selected combinations, no carry-only measurement claim.'if phase=='fonts'else'Actual production UI with final full catalog; only explicitly visible states are claimed.','jobs':records}
 for tag,view,w,h,growth,mode,args in jobs:
  out=base/tag;assert not out.exists(),f'Use a fresh output; never overwrite completed evidence: {out}';out.mkdir()
  cmd=[str(binary),mode,str(R),str(pilot),str(out),str(w),str(h)]+args
  if phase!='fonts':cmd=['xvfb-run','-a','-s','-screen 0 1600x1400x24']+cmd
  env=environment(out/'profile',view=='phone',growth);start=time.monotonic();print('START',tag,flush=True)
  with (out/'capture.log').open('w')as log:
   try:rc=run_font_bounded(cmd,env,log,out/'storage-observation.json')if phase=='fonts'else subprocess.run(cmd,cwd=R,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=900).returncode
   except subprocess.TimeoutExpired:rc='timeout'
  record={'tag':tag,'command':cmd,'returncode':rc,'seconds':round(time.monotonic()-start,2),'text_size_percent':growth};records.append(record);write(execution,identity)
  if rc==0 and phase=='fonts':
   manifest=json.loads((out/'font-matrix.json').read_text());assert len(manifest['samples'])==len(expected)
   assert {(s['key'],s['surface'],s['branch'])for s in manifest['samples']}=={(k,surf,branch)for surf,k,branch in expected}
   raw=(out/'font-matrix.json').read_bytes()
   with gzip.open(out/'font-matrix.json.gz','wb',compresslevel=9)as target:target.write(raw)
   assert gzip.decompress((out/'font-matrix.json.gz').read_bytes())==raw
   record['diagnostics_sha256']=hashlib.sha256(raw).hexdigest();record['complete_diagnostics_gzip']=str(out/'font-matrix.json.gz');(out/'font-matrix.json').unlink();write(execution,identity)
  print('END',tag,rc,flush=True)
  if rc!=0:raise SystemExit(f'Failed {tag}; evidence retained, later jobs not counted.')

def run_tests(binary,kind,revision):
 guard(binary,revision);out=B/('native-tests-v9-'+kind);assert not out.exists(),'Do not overwrite earlier test evidence';out.mkdir()
 filters=['ApiErrorText/*','PlatformProtocol/*','PlatformClient/*','UILayout/*']if kind=='unit'else['ScriptEditor/*','HiveMindIntegration/*','HiveMindScheduler/*','HiveMindReliability/*','HiveMindPresentation/*','OnlineScreenLifetime/*','FontKerning/*','SystemLanguage/*','SettingsAILibrary/*','GUIInteractionCoverage/Bulgarian*','CommunityMusicUI/API failures clear queued installs*','GameGUITouch/phone navigation*','GameGUITouch/live statistics hide post-game probability*','GUIInteractionCoverage/statistics topics history and overlays*','Settings/*all categories with enlarged text*','Settings/*software rendering*','LegacyScriptCoverage/*']
 build=R/'build/linux/client/debug/dev-dev_fast-true-linker-auto'
 assert binary.resolve()==(build/'test'/('glob2-'+kind+'-tests')).resolve(),'Use actual final linked test binary'
 cmd=['xvfb-run','-a','-s','-screen 0 1600x1400x24','python3','test/run_tests.py','--build-dir',str(build),'--binary',kind,'-j','2','--display-jobs','1','--timeout','600','--junit',str(out/'junit.xml'),'--artifacts',str(out),'--write-inventory',str(out/'inventory.json'),'--write-timings',str(out/'timings.json')]
 for f in filters:cmd+=['--filter',f]
 env=dict(os.environ,XDG_SESSION_TYPE='x11',LD_LIBRARY_PATH=str(B/'sdl3/prefix/lib')+':'+os.environ.get('LD_LIBRARY_PATH',''))
 for k in ['WAYLAND_DISPLAY','WAYLAND_SOCKET','SDL_VIDEODRIVER','SDL_VIDEO_DRIVER']:env.pop(k,None)
 record={'revision':revision,'binary':str(binary),'binary_sha256':sha(binary),'command':cmd,'filters':filters};write(out/'command.json',record);start=time.monotonic()
 with (out/'run.log').open('w')as log:rc=subprocess.run(cmd,cwd=R,env=env,stdout=log,stderr=subprocess.STDOUT).returncode
 names=json.loads((out/'inventory.json').read_text())['assigned'];record.update(returncode=rc,seconds=round(time.monotonic()-start,2),matched_filters={f:sum(fnmatch.fnmatchcase(n,f)for n in names)for f in filters});write(out/'command.json',record);assert all(record['matched_filters'].values()),record['matched_filters'];raise SystemExit(rc)
if __name__=='__main__':
 assert len(sys.argv)==5 and sys.argv[3]=='--handoff','phase BINARY --handoff REVISION required'
 phase=sys.argv[1];binary=Path(sys.argv[2]).resolve();revision=sys.argv[4]
 if phase in ['fonts','surfaces','schools']:run_capture(binary,phase,revision)
 elif phase in ['unit','engine']:run_tests(binary,phase,revision)
 else:raise SystemExit('Unknown phase')
