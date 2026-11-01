"""Ignored native QA orchestration. Explicit phases; never commits or starts implicitly."""
from pathlib import Path
import argparse,gzip,hashlib,json,os,shlex,shutil,signal,subprocess,time
ROOT=Path(__file__).resolve().parents[4]
BASE=Path(__file__).resolve().parent
GOLDENS=('test/fixtures/multiplayer/FourSquares1.g2mr','test/fixtures/multiplayer/FourSquares1.verify-trace.txt')
BUILD=ROOT/'build/linux/client/debug/dev-dev_fast-true-linker-auto'
ENV=dict(os.environ)
ENV.update(json.loads((ROOT/'artifacts/marathi-localization/integration-bc8/native/build-env.json').read_text(encoding='utf-8')))
ENV['LD_LIBRARY_PATH']=str(ROOT/'artifacts/marathi-localization/native-deps/lib')+':'+ENV.get('LD_LIBRARY_PATH','')
for key in ('DISPLAY','WAYLAND_DISPLAY','SDL_VIDEODRIVER','SDL_VIDEO_DRIVER'): ENV.pop(key,None)
ENV.update(SDL_AUDIODRIVER='dummy',XDG_SESSION_TYPE='x11')
CAP=1536*1024**2; FREE=2*1024**3

def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  while b:=f.read(1024**2): h.update(b)
 return h.hexdigest()
def write(name,value): (BASE/name).write_text(json.dumps(value,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
def git(*args): return subprocess.check_output(['git',*args],cwd=ROOT,text=True).strip()
def usage(): return sum(p.stat().st_size for p in BASE.rglob('*') if p.is_file() and not p.is_symlink())
def guard():
 if usage()>CAP or shutil.disk_usage(BASE).free<FREE: raise RuntimeError('Evidence storage limit/free-space guard reached; not success')
def tree_hashes():
 return {p:sha(ROOT/p) for p in git('ls-files').splitlines() if (ROOT/p).is_file() and p not in GOLDENS}
def binding(): return {'revision':git('rev-parse','HEAD'),'status':git('status','--porcelain'),'tracked_input_sha256':tree_hashes()}
def check_inputs(info):
 assert tree_hashes()==info['tracked_input_sha256'],'Tracked non-golden inputs changed after frozen producer build'
 changed=git('diff','--name-only','HEAD').splitlines()
 assert set(changed)<=set(GOLDENS),changed

def run(label,cmd,timeout=3600,video=None):
 """Bounded log capture, no environment dumps; only own process-group termination."""
 guard(); log=BASE/(label+'.log.gz'); record={'command':cmd,'cwd':str(ROOT),'deadline_seconds':timeout,'started_unix':time.time()}
 task_env=dict(ENV)
 if video: task_env.update(SDL_VIDEO_DRIVER=video,SDL_VIDEODRIVER=video)
 record['explicit_video_driver']=video
 process=subprocess.Popen(cmd,cwd=ROOT,env=task_env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
 import threading
 error=[]
 def capture():
  try:
   with gzip.open(log,'wb') as f:
    while chunk:=process.stdout.read1(65536):
     f.write(chunk);f.flush()
  except Exception as e: error.append(str(e))
 thread=threading.Thread(target=capture);thread.start()
 try:
  while process.poll() is None:
   time.sleep(2);guard()
   if time.time()-record['started_unix']>timeout: raise TimeoutError(label+' deadline reached')
 except BaseException:
  os.killpg(process.pid,signal.SIGTERM)
  try: process.wait(timeout=15)
  except subprocess.TimeoutExpired: os.killpg(process.pid,signal.SIGKILL);process.wait()
  raise
 finally:
  thread.join(timeout=15)
  record.update(exit_code=process.returncode,elapsed_seconds=time.time()-record['started_unix'],log_sha256=sha(log),capture_errors=error)
  write(label+'-command.json',record)
 if error or process.returncode: raise RuntimeError(label+' failed; retain log and command record')
 return log

ENGINE_CASES=[
 '#libgag/src/FontKerningTest.cpp','#src/app/SystemLanguageTest.cpp',
 '#src/scripting/sgsl/LegacyScriptCoverageTest.cpp','#src/scripting/javascript/ScriptCompatibilityTest.cpp',
 '#src/online/MatchSetupTest.cpp','#src/net/turn/TurnEngineHarness.cpp',
 '#src/hud/GUIInteractionCoverageTest.cpp','#src/hud/touch/GameGUITouchHarness.cpp']
FILTERS=['LegacyScriptCoverage/*','FontKerning/*','SystemLanguage/*','MatchSetup/*',
 'JavaScriptCompatibility/Current simulation rejects earlier replays and enforces acceptance boundaries*',
 'GUIInteractionCoverage/statistics*',
 'GameGUITouch/live statistics hide post-game probability across topics*',
 'GameGUITouch/tutorial wrapping preserves shaped clusters and refreshes font metrics*',
 'TurnEngineHarness/Marathi registration rejects earlier match identities*',
 'TurnEngineHarness/the committed*',
 'TurnEngineHarness/two engines with AI seats under latency*']

def test_command(filters,out):
 cmd=['python3','test/run_tests.py','--build-dir',str(BASE/'focused-build'),'--binary','engine','--artifacts',str(BASE/out),'--junit',str(BASE/out/'junit.xml'),'--timeout','600','--display-jobs','1','-j2','--verbose']
 for f in filters: cmd+=['--filter',f]
 return cmd

p=argparse.ArgumentParser();p.add_argument('--phase',choices=['build','golden-update','verify'],required=True);p.add_argument('--revision',required=True);args=p.parse_args()
if args.phase=='build':
 assert git('rev-parse','HEAD')==args.revision and not git('status','--porcelain'),'Require exact CLEAN frozen revision'
 assert '#define SIM_REVISION 112' in (ROOT/'src/game/SimRevision.h').read_text(encoding='utf-8')
 info=binding();write('frozen-inputs.json',info)
 # Official dependency validation is executed by dev_build; old prefix receipt is not relabelled.
 log=run('official-build',['python3','tools/dev_build.py','translation-preview-harness','glob2','-j4'],7200)
 check_inputs(info)
 import sys
 sys.path.insert(0,str(ROOT/'test'));import tests as registry
 actual={e if isinstance(e,str) else e[0] for e in registry.ENGINE_TESTS}
 assert set(ENGINE_CASES)<=actual,'Every selected test TU must remain officially registered'
 sources=registry.SUPPORT+registry.ENGINE_SUPPORT+ENGINE_CASES
 objects=[BUILD/'test'/('engine-'+s.lstrip('#').replace('/','_').rsplit('.',1)[0]+'.o') for s in sources]
 run('focused-object-build',['python3','tools/dev_build.py',*[str(x.relative_to(ROOT)) for x in objects],'-j4'],3600)
 # Obtain exact official production link recipe even when compilation was cached.
 dry=run('official-link-recipe',['python3','tools/dev_build.py','translation-preview-harness','-n','-B','-j1'],300)
 lines=gzip.open(dry,'rt',encoding='utf-8').read().splitlines()
 line=next(l for l in lines if l.startswith('g++ -o ') and '/TranslationPreviewHarness ' in l)
 link=shlex.split(line);idx=next(i for i,s in enumerate(link) if s.endswith('/program-tools_TranslationPreviewHarness.o'))
 link[link.index('-o')+1]=str(BASE/'focused-engine-tests');link[idx:idx+1]=[str(x) for x in objects]
 run('focused-link',link,600)
 slot=BASE/'focused-build/test';slot.mkdir(parents=True,exist_ok=True)
 (slot/'glob2-engine-tests').symlink_to(BASE/'focused-engine-tests')
 run('focused-inventory',[str(BASE/'focused-engine-tests'),'-ltc'],300)
 run('relay-build',['python3','tools/dev_build.py','role=relay','relay-tests','-j4'],3600)
 relay=ROOT/'build/linux/relay/debug/dev-dev_fast-true-linker-auto/src/glob2-relay-tests'
 assert relay.is_file(),str(relay)
 info.update(focused_binary=str(BASE/'focused-engine-tests'),focused_sha256=sha(BASE/'focused-engine-tests'),official_harness_sha256=sha(BUILD/'test/TranslationPreviewHarness'),native_cli_sha256=sha(BUILD/'src/glob2'),registered_sources=sources,relay_binary=str(relay),relay_sha256=sha(relay),environment_prefixes={k:ENV[k] for k in ('GLOB2_SDL3_PREFIX','GLOB2_RECORDING_PREFIX')})
 check_inputs(info);write('build-ready.json',info)
elif args.phase=='golden-update':
 info=json.loads((BASE/'build-ready.json').read_text(encoding='utf-8'));assert info['revision']==args.revision;check_inputs(info)
 before={g:sha(ROOT/g) for g in GOLDENS}
 cmd=test_command(['TurnEngineHarness/the committed*'],'golden-update');cmd.append('--update-fixtures');run('golden-update',cmd,1200,video='dummy')
 check_inputs(info)
 write('golden-fixture-mutation.json',{'actual_producer_revision':info['revision'],'producer_sha256':info['focused_sha256'],'before':before,'after':{g:sha(ROOT/g) for g in GOLDENS},'only_allowed_mutations':list(GOLDENS),'root_fixture_commit_required':True})
 print('Golden fixtures updated. Root must inspect and commit exactly these fixtures; no commit performed.',flush=True)
else:
 info=json.loads((BASE/'build-ready.json').read_text(encoding='utf-8'));assert info['revision']==args.revision;check_inputs(info)
 headless=test_command(FILTERS,'focused-headless-results')+['--exclude-tag','display']
 run('focused-headless-verify',headless,3600,video='dummy')
 display=test_command(FILTERS,'focused-display-results')+['--tag','display']
 run('focused-display-verify',display,1800,video='x11')
 import xml.etree.ElementTree as ET
 counts={};passed_cases=[]
 for folder in ('focused-headless-results','focused-display-results'):
  junit=ET.parse(BASE/folder/'junit.xml').getroot()
  assert not any(list(junit.iter(x)) for x in ('failure','error','skipped')),folder
  passed_cases.extend({'suite':x.attrib.get('classname'),'case':x.attrib.get('name')} for x in junit.iter('testcase'))
  counts[folder]=len(list(junit.iter('testcase')))
  assert counts[folder]>0,folder
 names={x['case'] for x in passed_cases}
 for required in ('Marathi SGSL language tokens require the installed catalog','Marathi SGSL selects its message and retains an authored default for other locales','adding Marathi preserves legacy SGSL source bytes and saved program counters','Malay SGSL language tokens require the installed catalog','adding Malay preserves legacy SGSL source bytes and saved program counters'):
  assert required in names,required
 write('focused-case-counts.json',{'counts':counts,'passed_cases':passed_cases})
 run('relay-admission',[info['relay_binary'],'--test-suite=RelayAdmission','--reporters=junit','--out='+str(BASE/'relay-admission.xml')],300)
 run('sim-revision-check',['python3','test/check_sim_revision.py','--base','ffabe1b9ef4e2a9986b1d6321c356124480b8a2d'],300)
 # Current CLI trace from exactly the same committed record/map/orders/seed.
 output=BASE/'native-match-verify';assert not output.exists(),'Never reuse a partial CLI verification directory'
 run('native-match-verify',[str(BUILD/'src/glob2'),'match','verify',str(ROOT/GOLDENS[0]),'--map-file',str(ROOT/'maps/FourSquares1.map.gz'),'--output-dir',str(output)],600,video='dummy')
 verdict=json.loads((output/'verdict.json').read_text(encoding='utf-8'));assert verdict['verdict']=='verified'
 assert (output/'checksums.txt').read_bytes()==(ROOT/GOLDENS[1]).read_bytes(),'Full per-tick native trace mismatch'
 write('verification-complete.json',{'actual_compiled_revision':info['revision'],'current_source_revision':git('rev-parse','HEAD'),'non_golden_input_carry_verified':True,'full_trace_sha256':sha(output/'checksums.txt'),'full_trace_line_count':len((output/'checksums.txt').read_bytes().splitlines()),'verdict':verdict['verdict'],'test_filters':FILTERS,'limits':'Focused official registry objects linked to exact official production objects; not the full engine/unit aggregate. Standalone replay format boundary tested by JavaScriptCompatibility plus generated replay load in registration test. No Android/Windows/macOS/iOS/browser determinism claimed here.'})
