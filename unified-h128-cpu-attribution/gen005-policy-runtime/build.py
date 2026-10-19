from pathlib import Path
import json,hashlib,subprocess,shlex,os,datetime,concurrent.futures,traceback
P=Path(__file__).resolve().parent;R=Path('/Users/bradley/.codex/worktrees/gradient-attribution-modern/glob2');OLD=Path('/Users/bradley/.codex/worktrees/gradient-master-integration/glob2');C=P.parents[1];revision='4cf3933fee8cfc63d1ddab802589dac6b0ffeb2e';original='3781b88988069b18b7588a50608c652aec2459a4ef81a30ae4c80777843a9236';h=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(v):(P/'BUILD-STATUS.json').write_text(json.dumps(v,indent=2)+'\n')
state=dict(status='BUILDING',pid=os.getpid(),source_revision=revision,patch_sha256=h(P/'research-policy.patch'),research_only=True,started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat());save(state)
try:
 os.nice(15)
 assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=R,text=True).strip()==revision
 assert not subprocess.check_output(['git','diff','--name-only',revision,'--','src','libgag','libusl','SConstruct','scons'],cwd=OLD,text=True).strip()
 assert h(OLD/'build/candidate-release/src/glob2')==original
 if not (R/'build').exists():os.symlink(OLD/'build',R/'build',target_is_directory=True)
 assert (R/'build').is_symlink() and (R/'build').resolve()==OLD/'build'
 changes=['src/map/MapStep.cpp','src/app/cli/Headless.cpp','src/map/gradient/ResourceGradientProbe.cpp']
 actual=subprocess.check_output(['git','diff','--name-only'],cwd=R,text=True).splitlines();assert set(actual)==set(changes)
 # Reuse is valid because headers/flags/generated config match and no changed
 # header or ABI exists. Only the three implementation objects are replaced.
 lines=(C/'macos-build.log').read_text().splitlines();commands=[];replace={}
 for f in changes:
  target='build/candidate-release/'+str(Path(f).with_suffix('.o'))
  line=next(x for x in reversed(lines) if ' -o '+target+' ' in x and ' -c ' in x)
  a=shlex.split(line);obj=P/(Path(f).stem+'.o');a[a.index('-o')+1]=str(obj);assert a[-1]==f;a[-1]=str(R/f);replace[target]=str(obj)
  commands.append(dict(argv=a,cwd=str(R),source=f,source_sha256=h(R/f),old_object_sha256=h(OLD/target)))
 (P/'compile-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 def compile_one(c):
  with (P/(Path(c['source']).stem+'.log')).open('w') as log:subprocess.run(c['argv'],cwd=R,check=True,stdout=log,stderr=subprocess.STDOUT)
 with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(compile_one,commands))
 link=next(shlex.split(x) for x in reversed(lines) if x.startswith('g++ -o build/candidate-release/src/glob2 '));link[link.index('-o')+1]=str(P/'glob2');link=[replace.get(x,x) for x in link]
 reused={x:h(OLD/x) for x in link if x.endswith(('.o','.a')) and not x.startswith(str(P))};(P/'reused-link-inputs.json').write_text(json.dumps(reused,indent=2)+'\n');(P/'link-command.json').write_text(json.dumps(dict(argv=link,cwd=str(OLD)),indent=2)+'\n')
 with (P/'link.log').open('w') as log:subprocess.run(link,cwd=OLD,check=True,stdout=log,stderr=subprocess.STDOUT)
 assert h(OLD/'build/candidate-release/src/glob2')==original
 assert all(h(OLD/f)==v for f,v in reused.items())
 state.update(status='BUILT',binary=str(P/'glob2'),binary_sha256=h(P/'glob2'),ordinary_release=True,reused_inputs_verified=len(reused),compiler=subprocess.check_output(['g++','--version'],text=True).splitlines()[0],finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat());save(state)
except BaseException:
 state.update(status='FAILED',error=traceback.format_exc());save(state);raise
