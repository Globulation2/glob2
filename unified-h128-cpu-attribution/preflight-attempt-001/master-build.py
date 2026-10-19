import pathlib,subprocess,json,datetime,hashlib,os,shutil,traceback
B=pathlib.Path(__file__).resolve().parent;W=pathlib.Path('/Users/bradley/.codex/worktrees/gradient-attribution-master/glob2');source=json.loads((B/'SOURCE.json').read_text());assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=W,text=True).strip()==source['base_source'];assert hashlib.sha256(subprocess.check_output(['git','diff','--binary'],cwd=W)).hexdigest()==source['patch_sha256']
def write(n,v):(B/n).write_text(json.dumps(v,indent=2)+'\n')
argv=['scons','release=1','CXXFLAGS=','dev_fast=0','pch=0','unity=0','linker=default','--build=build/research-release','-j3','build/research-release/src/glob2'];start=datetime.datetime.now(datetime.timezone.utc).isoformat()
try:
 with (B/'build.log').open('w') as log:
  child=subprocess.Popen(argv,cwd=W,stdout=log,stderr=subprocess.STDOUT);write('BUILD.json',dict(status='BUILDING',supervisor_pid=os.getpid(),pid=child.pid,argv=argv,started_utc=start,ordinary_dependency_graph=True));code=child.wait()
 assert code==0,'Research master build failed'
 assert hashlib.sha256(subprocess.check_output(['git','diff','--binary'],cwd=W)).hexdigest()==source['patch_sha256'],'Source changed during compilation'
 (B/'bin').mkdir();binary=B/'bin/glob2-master-class-research';shutil.copy2(W/'build/research-release/src/glob2',binary);write('BUILD.json',dict(status='PASS',source=source,argv=argv,started_utc=start,finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),binary=str(binary),binary_sha256=hashlib.file_digest(binary.open('rb'),'sha256').hexdigest(),ordinary_dependency_graph=True))
except Exception:
 write('BUILD-FAILURE.json',dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),error=traceback.format_exc()));raise
