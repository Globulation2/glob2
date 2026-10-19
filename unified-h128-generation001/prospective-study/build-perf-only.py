import datetime,fcntl,hashlib,json,os,pathlib,shlex,subprocess,traceback
B=pathlib.Path(__file__).resolve().parent;O=B/'perf-build';O.mkdir(exist_ok=True);S=O/'source';OLD=pathlib.Path('/home/bradley/glob2-gradient-unified-h128');P=json.loads((B/'PLAN.json').read_text())
lock=(O/'lock').open('w');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def cmd(a,**kw):return subprocess.run(a,check=True,**kw)
def save(s):(O/'STATUS.json').write_text(json.dumps(s,indent=2)+'\n')
state={'status':'building','supervisor_pid':os.getpid(),'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_base':P['source_revision'],'scope':'isolated performance-only3validation-guard patch; ordinary release objects reused with exact compiler/dependency inputs; not collector qualification'};save(state)
try:
 assert subprocess.check_output(['git','-C',str(OLD),'rev-parse','HEAD'],text=True).strip()==P['source_revision']
 assert not subprocess.check_output(['git','-C',str(OLD),'status','--porcelain'],text=True).strip()
 assert sha(OLD/'build/unified-release/src/glob2')==P['frozen_binary_sha256']
 cmd(['git','clone','--shared','--no-checkout',str(OLD),str(S)],stdout=subprocess.DEVNULL)
 cmd(['git','-C',str(S),'checkout','--detach',P['source_revision']],stdout=subprocess.DEVNULL)
 os.symlink(OLD/'build',S/'build',target_is_directory=True) # generated headers read-only; outputs remain under O
 changes={
 'src/app/cli/CommandLine.cpp':('        requireParent("--resource-gradient-exploration", "--resource-gradient-probe");','        // PERFORMANCE-ONLY: permit fixed research behavior without quality observation.'),
 'src/app/cli/Headless.cpp':('if (one(options,"--resource-gradient-probe","false")!="true" || recordReplay)','if ((options.count("--resource-gradient-fork-field") && one(options,"--resource-gradient-probe","false")!="true") || recordReplay)'),
 'src/map/MapStep.cpp':('if (!globalContainer || !globalContainer->structuredHeadless || !gradientRuntime->qualityProbe)','if (!globalContainer || !globalContainer->structuredHeadless || (forkField>=0 && !gradientRuntime->qualityProbe))')}
 for f,(old,new)in changes.items():
  p=S/f;s=p.read_text();assert s.count(old)==1,(f,s.count(old));p.write_text(s.replace(old,new))
 (O/'validation-only.patch').write_bytes(subprocess.check_output(['git','-C',str(S),'diff','--',*changes]))
 logs=[(B.parent/'linux/build.log'),OLD.parent/'glob2/artifacts/gradient-unified-20261019/builds/linux/build-fix1.log',OLD.parent/'glob2/artifacts/gradient-unified-20261019/builds/linux/build-f64.log']
 lines=[line for log in logs if log.exists()for line in log.read_text().splitlines()]
 replacements={};commands=[]
 for f in changes:
  target='build/unified-release/'+str(pathlib.Path(f).with_suffix('.o'))
  line=next(line for line in reversed(lines)if ' -o '+target+' 'in line and ' -c 'in line)
  a=shlex.split(line);obj=O/(pathlib.Path(f).stem+'.o');a[a.index('-o')+1]=str(obj)
  assert a[-1]==f,(f,a[-1]);a[-1]=str(S/f)
  replacements[target]=str(obj);commands.append({'argv':a,'cwd':str(S),'source':f,'source_sha256':sha(S/f),'old_object_sha256':sha(OLD/target)})
  (O/'commands.json').write_text(json.dumps(commands,indent=2))
  with(O/(pathlib.Path(f).stem+'.log')).open('w')as log:cmd(['taskset','-c','4','nice','-n','19',*a],cwd=S,stdout=log,stderr=subprocess.STDOUT)
 link=next(shlex.split(line)for line in reversed(lines)if line.startswith('g++ -o build/unified-release/src/glob2 '))
 binary=O/'glob2';link[link.index('-o')+1]=str(binary);link=[replacements.get(x,x)for x in link]
 reused={str(x):sha(OLD/x)for x in link if x.endswith(('.o','.a')) and not x.startswith(str(O))}
 (O/'reused-link-inputs.json').write_text(json.dumps(reused,indent=2));(O/'link-command.json').write_text(json.dumps({'argv':link,'cwd':str(OLD)},indent=2))
 with(O/'link.log').open('w')as log:cmd(['taskset','-c','4','nice','-n','19',*link],cwd=OLD,stdout=log,stderr=subprocess.STDOUT)
 assert sha(OLD/'build/unified-release/src/glob2')==P['frozen_binary_sha256']
 state.update(status='built',binary=str(binary),binary_sha256=sha(binary),patch_sha256=sha(O/'validation-only.patch'),compiler=subprocess.check_output(['g++','--version'],text=True).splitlines()[0],finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat());save(state)
except BaseException as e:
 state.update(status='failed',error=repr(e),traceback=traceback.format_exc());save(state);raise
