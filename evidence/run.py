import os,json,time,sys,importlib.util,shutil,gzip,traceback,contextlib
from pathlib import Path
R=Path(__file__).resolve().parents[3];B=R/'artifacts/ci-recovery';D=B/'reference-master127';W=B/'recording-download-repair';C=W/'artifacts/ci-recovery/reference-master127';C.mkdir(parents=True,exist_ok=False);state=dict(pid=os.getpid(),phase='fresh current master601926 simulation127/format205 candidate checks',results=[],candidate_directory=str(C),budget_bytes=2*1024**3,minimum_free_bytes=20*1024**3,note='Candidateexpectedtraceswrittenonlyunderignoredartifacts;canonicalfixturesandfrozenv108/v115inputsunchanged. Maintainedcompletechecksumcheckers,serialthenparallel,independentmidpointreload;current master601926 production/SIM127 format205 with schema-only PR1417 patch; fresh native candidate generation for existing1412; original released saves unchanged. No testweakening or automatic baseline promotion.')
def save():(D/'status.json').write_text(json.dumps(state,indent=2)+'\n')
def budget():
 total=sum(p.stat().st_size for p in C.rglob('*')if p.is_file());state['retained_bytes']=total
 if total>state['budget_bytes']:raise RuntimeError('candidate evidence exceeds2GiBbudget;failure retained,not counted as success')
 if shutil.disk_usage(C).free<state['minimum_free_bytes']:raise RuntimeError('free space below20GiBfloor')
def module(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
save()
state['revision']=__import__('subprocess').check_output(['git','rev-parse','HEAD'],cwd=W,text=True).strip();assert state['revision']=='e28aea45f10dc21f15e23cef84da94e55d888fec'
os.environ['LD_LIBRARY_PATH']=str(R/'build/sdl3-ci/prefix/lib');os.environ['PYTHONDONTWRITEBYTECODE']='1';os.environ['GLOB2_USER_DATA_DIR']=str(C/'profile');os.environ['SDL_VIDEODRIVER']='dummy';os.environ['SDL_AUDIODRIVER']='dummy';sys.path.insert(0,str(W/'test'));binary=W/'build/ci-schema-release/src/glob2'
m=module('current_maxima_checker',W/'test/maxima/check_save_continuation_fixture.py');m.EXPECTED=C/'expected-units-30000-30512.json';shutil.copy2(W/'test/maxima/fixtures/save-continuation/expected-units-30000-30512.json',m.EXPECTED)
t=module('current_telemetry_checker',W/'test/check_telemetry_simulation.py');cases=[]
for source,ticks,fixture in t.SCENARIOS:
 target=C/fixture.name;shutil.copy2(fixture,target);cases.append((source,ticks,target))
t.SCENARIOS=tuple(cases);original=t.PARENT_RELOAD;t.PARENT_RELOAD=C/original.name;shutil.copy2(original,t.PARENT_RELOAD)
steps=[('maxima-serial-candidate',lambda:m.check(binary,C/'maxima-serial',1,True)),('maxima-parallel-check',lambda:m.check(binary,C/'maxima-parallel',4,False)),('telemetry-serial-candidate',lambda:t.main(str(binary),False,C/'telemetry-serial',True)),('telemetry-parallel-check',lambda:t.main(str(binary),True,C/'telemetry-parallel',False))]
for name,action in steps:
 state['phase']=name;save();start=time.monotonic();code=0
 with gzip.open(D/(name+'.log.gz'),'wt')as log,contextlib.redirect_stdout(log),contextlib.redirect_stderr(log):
  try:
   budget();result=action();assert result in (None,0),result;budget()
  except BaseException:traceback.print_exc();code=1
 state['results'].append(dict(name=name,exit_code=code,seconds=round(time.monotonic()-start,1)));save()
 if code:break
state['phase']='finished; inspect candidate complete traces, continuity and cross-worker equality;no canonical fixtures edited';save()

if all(x['exit_code']==0 for x in state['results']):
 paths=['test/maxima/fixtures/save-continuation/expected-units-30000-30512.json','test/fixtures/scoped-gradients/gd-large-4ai-1024.units.checksums.gz','test/fixtures/scoped-gradients/gd-bigarena-2048.units.checksums.gz','test/fixtures/scoped-gradients/numbi-castor-2048.units.checksums.gz','test/fixtures/scoped-gradients/v108-reload-256.units.checksums.gz']
 import subprocess,hashlib
 equal={p:(C/Path(p).name).read_bytes()==subprocess.check_output(['git','show','031c13a4aebc0a879bb387180cbaeb4337624084:'+p],cwd=W)for p in paths};state['proposed_1412_byte_equality']=equal;save()

if len(state["results"]) != 4 or any(item["exit_code"] != 0 for item in state["results"]):
 raise SystemExit(1)
