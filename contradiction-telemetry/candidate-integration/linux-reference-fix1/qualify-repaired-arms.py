import datetime,hashlib,json,os,pathlib,shutil,subprocess,time
campaign=pathlib.Path('/home/bradley/glob2/artifacts/gradient-contradictions-20261018/candidate-next');reference=campaign/'linux-reference-fix1';candidate=campaign/'linux-candidate-fix1';cpus=[1,2,4,5,8,10,12,14]
now=lambda:datetime.datetime.now(datetime.timezone.utc).isoformat()
def write(folder,name,data):
 p=folder/name;t=p.with_suffix(p.suffix+'.tmp');t.write_text(json.dumps(data,indent=2)+'\n');t.replace(p)
def before():os.nice(19);os.sched_setaffinity(0,cpus)
def command(folder,name,argv,root,env=None):
 write(folder,name+'-command.json',{'argv':argv,'cwd':str(root),'started':now(),'cpus':cpus,'nice':19})
 with (folder/(name+'.log')).open('w') as log:
  p=subprocess.Popen(argv,cwd=root,stdout=log,stderr=subprocess.STDOUT,env=env,preexec_fn=before)
  write(folder,name+'-status.json',{'status':'running','pid':p.pid,'started':now()});rc=p.wait()
 write(folder,name+'-status.json',{'status':'passed' if rc==0 else 'failed','returncode':rc,'finished':now()})
 if rc:raise RuntimeError(name+' failed; inspect '+str(folder/(name+'.log')))
def qualify(folder,root,build):
 for binary,filters in [('engine',['ResourceGradientPolicy/*','DelayedGradientPublication/*','InitialBuildingGradient/*','BuildingGradientInvalidation/*','TeamStatsPipeline/*','TeamStatsSave/*','CommandLine/*']),('unit',['ReplayStepCounter/*','Gradient/*','ResourceGradientContradictions/*'])]:
  args=['python3','test/run_tests.py','--build-dir',build,'--binary',binary,'-j2','--no-display','--exclude-tag','benchmark']
  for f in filters:args+=['--filter',f]
  args+=['--junit',str(folder/(binary+'-focused.xml')),'--artifacts',str(folder/(binary+'-evidence')),'--write-inventory',str(folder/(binary+'-inventory.json'))]
  command(folder,binary+'-focused',args,root)
  import fnmatch,xml.etree.ElementTree as ET
  inventory=json.loads((folder/(binary+'-inventory.json')).read_text());assigned=inventory['assigned']
  counts={pattern:sum(fnmatch.fnmatchcase(name,pattern) for name in assigned) for pattern in filters}
  assert assigned and all(counts.values()),counts
  xml=ET.parse(folder/(binary+'-focused.xml')).getroot()
  tests=sum(1 for _ in xml.iter('testcase'))
  assert tests>0,(binary,ET.tostring(xml).decode()[:500])
  write(folder,binary+'-coverage-verified.json',{'status':'verified','filters':counts,'assigned':len(assigned),'xml_testcases':tests,'inventory_fingerprint':inventory['fingerprint']})
 frozen=folder/'frozen';frozen.mkdir(exist_ok=True);outputs={}
 for p in (root/build).rglob('*'):
  if p.is_file() and p.name in ['glob2','glob2-engine-tests','glob2-unit-tests']:
   target=frozen/p.name;shutil.copy2(p,target);outputs[p.name]={'path':str(target),'sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'bytes':target.stat().st_size}
 for source in ['tools/models/resource-gradient.json','src/map/gradient/ResourceGradientModel.h']:
  shutil.copy2(root/source,frozen/pathlib.Path(source).name)
 write(folder,'qualified.json',{'status':'qualified','revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'time':now(),'outputs':outputs,'tracked_clean':not subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=root),'toolchain':subprocess.check_output(['g++','--version'],text=True),'uname':subprocess.check_output(['uname','-a'],text=True)})

def verify(folder,root):
 prov=json.loads((folder/'source-provenance.json').read_text());manifest=json.loads((folder/'source-file-sha256.json').read_text())
 assert hashlib.sha256((folder/'source.bundle').read_bytes()).hexdigest()==prov['bundle_sha256']
 assert hashlib.sha256((folder/'source-file-sha256.json').read_bytes()).hexdigest()==prov['manifest_sha256']
 assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()==prov['revision']
 import stat
 for m in manifest:
  path=root/m['path'];data=os.readlink(path).encode() if path.is_symlink() else path.read_bytes()
  assert hashlib.sha256(data).hexdigest()==m['sha256'],m['path']
  assert path.is_symlink() or stat.S_IMODE(path.lstat().st_mode)&0o111==int(m['git_mode'],8)&0o111,m['path']
 assert not subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=root)
 for path,key in [('tools/models/resource-gradient.json','model_sha256'),('src/map/gradient/ResourceGradientModel.h','header_sha256')]:assert hashlib.sha256((root/path).read_bytes()).hexdigest()==prov[key]
 write(folder,'source-verified.json',{'status':'verified','revision':prov['revision'],'files':len(manifest),'manifest_sha256':prov['manifest_sha256'],'time':now()})
 return prov
try:
 write(reference,'supervisor-status.json',{'status':'waiting-original-reference-wave','pid':os.getpid(),'time':now()})
 original=campaign/'linux-reference'
 while True:
  p=original/'qualification-supervisor-status.json'
  state=json.loads(p.read_text()) if p.exists() else {'status':'missing'}
  if state['status'] in ['failed','all-qualified']:break
  time.sleep(20)
 build=json.loads((original/'build-status.json').read_text())
 if build['status']!='passed':raise RuntimeError('Original frozen reference failed to compile, inspect original build before retry')
 frozen=original/'frozen-unqualified';frozen.mkdir(exist_ok=True)
 for p,h in build['outputs'].items():
  target=frozen/pathlib.Path(p).name;shutil.copy2(p,target);assert hashlib.sha256(target.read_bytes()).hexdigest()==h
 write(original,'unqualified-frozen.json',{'status':'original-wave-preserved','build':build,'qualification':state,'frozen':str(frozen),'time':now()})
 root=pathlib.Path('/home/bradley/glob2-gradient-reference-54cdc48bf')
 previous='54cdc48bf240e8779b3d926107b323ecdad85926'
 for folder in [reference,candidate]:
  assert not subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=root)
  assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()==previous
  metadata=json.loads((folder/'source-provenance.json').read_text())
  assert hashlib.sha256((folder/'source.bundle').read_bytes()).hexdigest()==metadata['bundle_sha256']
  heads=subprocess.check_output(['git','bundle','list-heads',str(folder/'source.bundle')],text=True).splitlines()
  matching=[line.split()[1] for line in heads if line.split()[0]==metadata['revision']]
  assert len(matching)==1,heads
  subprocess.run(['git','fetch',str(folder/'source.bundle'),matching[0]],cwd=root,check=True)
  subprocess.run(['git','checkout','--detach',metadata['revision']],cwd=root,check=True)
  prov=verify(folder,root)
  write(folder,'build-root-reuse.json',{'cwd':str(root),'build':'build/reference-release','previous_completed_frozen_revision':previous,'revision':prov['revision'],'reason':'identical absolute compiled paths and ordinary release options preserve valid object reuse','source_manifest_sha256':prov['manifest_sha256'],'time':now()})
  env=os.environ.copy();env.update(CCACHE='1',CCACHE_MAXSIZE='12G',GLOB2_SDL3_PREFIX='/home/bradley/glob2/build/sdl3/prefix',GLOB2_RECORDING_PREFIX='/home/bradley/.local/share/glob2/development/dependencies/build-b2f8b01e7858d5108b038be8/prefix')
  write(reference,'supervisor-status.json',{'status':'building' if folder==reference else 'building-candidate','folder':str(folder),'time':now(),'pid':os.getpid()})
  command(folder,'build',['scons','release=1','CXXFLAGS=','--build=build/reference-release','-j8','build/reference-release/src/glob2','engine-tests','unit-tests'],root,env)
  qualify(folder,root,'build/reference-release')
  previous=prov['revision']
 write(reference,'supervisor-status.json',{'status':'both-qualified','time':now()})
except Exception as e:
 write(reference,'supervisor-status.json',{'status':'failed','diagnostic':str(e),'time':now()});raise
