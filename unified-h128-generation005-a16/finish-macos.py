import pathlib,subprocess,time,json,datetime,hashlib,os
B=pathlib.Path(__file__).resolve().parent
W=pathlib.Path('/Users/bradley/.codex/worktrees/gradient-master-integration/glob2')
def state(s,**kw):
 (B/'MACOS-FINAL-QUALIFICATION.json').write_text(json.dumps(dict(status=s,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),source='4cf3933fee8cfc63d1ddab802589dac6b0ffeb2e',**kw),indent=2)+'\n')
def run(name,args):
 state('RUNNING',step=name)
 with (B/(name+'.log')).open('w') as out:r=subprocess.run(args,cwd=W,stdout=out,stderr=subprocess.STDOUT)
 if r.returncode:state('FAILED',step=name,returncode=r.returncode);raise SystemExit(r.returncode)
state('WAITING-EXISTING-BUILD',pid=7849)
while subprocess.run(['ps','-p','7849'],stdout=subprocess.DEVNULL).returncode==0:time.sleep(5)
run('macos-final-incremental',['scons','release=1','CXXFLAGS=','dev_fast=0','pch=0','unity=0','linker=default','--build=build/candidate-release','-j6','build/candidate-release/src/glob2','build/candidate-release/test/glob2-engine-tests','build/candidate-release/test/glob2-unit-tests'])
common=['python3','test/run_tests.py','--build-dir','build/candidate-release','--binary','all']
run('final-goldens-update',common+['--tag','golden','--update-fixtures','--junit',str(B/'final-goldens-update.xml')])
run('final-goldens-verify',common+['--tag','golden','--junit',str(B/'final-goldens-verify.xml')])
run('final-state-replay',common+['--filter','AdaptiveGradientState/*','--filter','ReplayStepCounter/*','--junit',str(B/'final-state-replay.xml')])
run('macos-final-runtime',['python3',str(B/'runtime-macos/qualify.py')])
binary=W/'build/candidate-release/src/glob2'
state('PASSED',binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),runtime_status=json.loads((B/'runtime-macos/status.json').read_text())['status'])
