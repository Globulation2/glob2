#!/usr/bin/env python3
"""Focused GPU-sync qualification only; default plan, no game/performance runs."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,subprocess,sys,time,xml.etree.ElementTree as ET
ART=Path(__file__).resolve().parent;MASK={0,1,2,16,17,18};OUT=ART/'tests';VAR=ART/'variants/gpu-sync'
TEST='OpenCLGradient/isolated host event acquisition commits exact output or fails closed'
SELF_SHA=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
def load(p):return json.loads(Path(p).read_text())
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(p,d):
 p=Path(p);p.parent.mkdir(parents=True,exist_ok=True);temp=p.with_suffix('.tmp');temp.write_text(json.dumps(d,indent=2)+'\n');temp.replace(p)
def gate():
 assert load(ART/'gpu-build-complete.json')['complete'] is True
 for name in ('gpu-sync','gpu-recovery'):
  folder=ART/'variants'/name;built=load(folder/'manifest.json')
  assert built['state']=='complete'
  assert built['source_manifest_sha256']==sha(folder/'source-manifest.json'),('waiting final rebuilt preparation seal',name)
  for f,h in built['source_sha256'].items():assert sha(Path(built['root'])/f)==h,('waiting final rebuilt source',name,f)
 for p in Path('/proc').glob('[0-9]*/comm'):
  try:name=p.read_text().strip()
  except OSError:continue
  assert name not in ('cc1plus','cc1','g++-13','gcc-13','scons'),('compiler still active',p,name)
 assert sha(__file__)==SELF_SHA,'qualification wrapper changed'
 b=load(VAR/'manifest.json');src=Path(b['root'])
 assert sha(VAR/'source-manifest.json')==b['source_manifest_sha256'],'source preparation seal changed'
 for path,h in b['dependency_hashes'].items():assert sha(path)==h,('runtime dependency changed',path)
 for f,h in b['source_sha256'].items():assert sha(src/f)==h,('built source changed',f)
 for role in ('binary','engine_binary','unit_binary'):assert sha(b[role])==b[role+'_sha256']
 return b

def environment(extra):
 env={k:v for k,v in os.environ.items() if not k.startswith('GLOB2_')};env.update(extra);env['GLOB2_OPENCL_DEVICE']='0';return env

def focused(b,role,filters,label):
 d=OUT/label;assert not d.exists(),('fresh test directory required',d);d.mkdir(parents=True)
 junit=d/'junit.xml';inventory=d/'inventory.json';src=Path(b['root'])
 cmd=[sys.executable,str(src/'test/run_tests.py'),'--build-dir',str(src/'build/linux/client/release'),'--binary',role,'--no-display','--timeout','120','-j2','--junit',str(junit),'--write-inventory',str(inventory),'--artifacts',str(d)]
 for f in filters:cmd+=['--filter',f]
 env=environment({});start=time.monotonic()
 with (d/'runner.log').open('x') as log:result=subprocess.run(cmd,cwd=src,env=env,stdout=log,stderr=subprocess.STDOUT)
 attrs=ET.parse(junit).getroot().attrib
 passed=result.returncode==0 and int(attrs.get('tests',0))>0 and all(int(attrs.get(k,0))==0 for k in ('failures','errors','skipped'))
 r=dict(passed=passed,binary_role=role,binary_sha256=b[role+'_binary_sha256'],junit=str(junit),junit_sha256=sha(junit),testsselected=load(inventory)['assigned'],inventory=str(inventory),inventory_sha256=sha(inventory),command=cmd,environment={'GLOB2_OPENCL_DEVICE':'0'},affinity=sorted(MASK),wall_s=time.monotonic()-start,source_manifest_sha256=b['source_manifest_sha256'],build_manifest_sha256=sha(VAR/'manifest.json'),runner_sha256=sha(__file__),log_sha256=sha(d/'runner.log'))
 write(OUT/(label+'.json'),r);assert passed,('focused failed',label);return r

def native_case(b,role,label,test,fault):
 d=OUT/label;assert not d.exists();d.mkdir(parents=True)
 spec=importlib.util.spec_from_file_location('sync_test_runner',Path(b['root'])/'test/run_tests.py');T=importlib.util.module_from_spec(spec);sys.modules[spec.name]=T;spec.loader.exec_module(T)
 cases=[c for c in T.list_cases(Path(b[role+'_binary']),role) if c.label==test];assert len(cases)==1
 original=dict(os.environ);os.environ.clear();os.environ.update({k:v for k,v in original.items() if not k.startswith('GLOB2_')});os.environ.update(fault)
 args=argparse.Namespace(keep_profiles=True,no_display=True,fullscreen=False,update_fixtures=False,artifacts=d,verbose=False)
 try:result=T.run_job(T.Job(role,cases),args,Path(b['root'])/'build/linux/client/release')
 finally:os.environ.clear();os.environ.update(original)
 (d/'proof.log').write_text(result.output);(d/'native-junit.xml').write_text(result.junit)
 junit=d/'junit.xml';T.merge_junit([result],junit)
 return dict(passed=result.status=='pass',binary_role=role,binary_sha256=b[role+'_binary_sha256'],test_name=test,testsselected=[test],junit=str(junit),junit_sha256=sha(junit),proof_log=str(d/'proof.log'),proof_log_sha256=sha(d/'proof.log'),fault_environment=fault,affinity=sorted(MASK),native_timeout_s=120,status=result.status,wall_s=result.seconds,profile_directory=result.profile,source_manifest_sha256=b['source_manifest_sha256'],build_manifest_sha256=sha(VAR/'manifest.json'),runner_sha256=sha(__file__))

def markers(receipt,prefix):
 return [json.loads(line.split(prefix+' ',1)[1]) for line in Path(receipt['proof_log']).read_text().splitlines() if prefix+' ' in line]

def hardware(b,failure):
 label='gpu-sync-hardware-failed-wait' if failure else 'gpu-sync-hardware-normal'
 fault={'GLOB2_TEST_OPENCL_HOST_ACQUIRE':'1','GLOB2_OPENCL_DEVICE':'0'}
 if failure:fault['GLOB2_TEST_OPENCL_WAIT_FAILURE']='1'
 r=native_case(b,'unit',label,TEST,fault);raw=markers(r,'GLOB2_OPENCL_HOST_ACQUIRE_PROOF');proof=raw[0] if len(raw)==1 else {}
 passed=r['passed'] and len(raw)==1
 if failure:passed=passed and proof==dict(injected_wait_failure=1,cpu_fallbacks=1,gpu_executed=0,gpu_commits=0,host_waits=0,session_failed=1,ready_plans=0)
 else:passed=passed and all(proof.get(k)==v for k,v in dict(injected_wait_failure=0,cpu_fallbacks=0,gpu_executed=1,session_failed=0).items()) and all(proof.get(k,0)>0 for k in ('gpu_commits','host_waits','ready_plans'))
 r.update(passed=passed,actual_hardware=bool(passed),proof=proof,host_gpu_inventory=str(OUT/'gpu-sync-host-gpu-inventory.json'),host_gpu_inventory_sha256=sha(OUT/'gpu-sync-host-gpu-inventory.json'))
 write(OUT/(label+'.json'),r);assert passed,('hardware proof failed',label,r['status'],proof);return r

def field_oracles(b):
 field=native_case(b,'unit','gpu-sync-participant-field','OpenCLGradient/isolated configured participant fields match independent weighted blocked oracle',{'GLOB2_OPENCL_DEVICE':'0','GLOB2_TEST_OPENCL_PARTICIPANT_ORACLE':'1'})
 raw=markers(field,'GLOB2_OPENCL_PARTICIPANT_ORACLE_PROOF')
 passed=field['passed'] and len(raw)==3 and {p['requested_count'] for p in raw}=={1,3,6}
 for r in raw:
  count=r['requested_count'];passed=passed and r==dict(requested_count=count,observed_count=count,worker_slot_observed=int(count>1),oracle_cases=6,cpu_fields=6 if count==1 else 0,gpu_fields=0 if count==1 else 6)
 field.update(passed=passed,proof=raw);write(OUT/'gpu-sync-participant-field.json',field);assert passed,('configured field oracle failed',raw)
 gate()
 material=native_case(b,'engine','gpu-sync-participant-material','GradientPreparation/seed cells match scalar predicates across policies and changing overlays',{'GLOB2_OPENCL_DEVICE':'0'})
 mats=markers(material,'GLOB2_MATERIAL_PARTICIPANT_ORACLE_PROOF')
 passed=material['passed'] and len(mats)==5 and {p['requested_count'] for p in mats}=={1,2,3,4,6}
 for r in mats:passed=passed and r==dict(requested_count=r['requested_count'],observed_count=r['requested_count'],scalar_material_clear_guard=1)
 material.update(passed=passed,proof=mats);write(OUT/'gpu-sync-participant-material.json',material);assert passed,('configured material oracle failed',mats)
 write(OUT/'gpu-sync-field-oracles.json',dict(passed=True,engine_binary_sha256=b['engine_binary_sha256'],unit_binary_sha256=b['unit_binary_sha256'],source_sha256=b['source_sha256'],participant_counts=[1,3,6],material_participant_counts=[1,2,3,4,6],retained_extra_material_counts=[2,4],junit_sha256=[dict(path=r['junit'],sha256=r['junit_sha256']) for r in [field,material]],field_proof=raw,material_proof=mats,proof_receipts=[str(OUT/'gpu-sync-participant-field.json'),str(OUT/'gpu-sync-participant-material.json')],runs=[dict(field,proofs=field['proof']),dict(material,proofs=material['proof'])],qualification='Actual configured counts and eligible workerSlot observed;1serialCPU negative,3/6 genuine GPU field commits match independent heap oracle; material/periodic seeds match independent scalar predicates. No game/performance acceptance.'))

def main():
 p=argparse.ArgumentParser();p.add_argument('--run',action='store_true');args=p.parse_args()
 if not args.run:
  print(json.dumps(dict(plan_only=True,launches=0,focused=['engine GradientPreparation/*','unit OpenCLGradient/* + OpenCLRequiredControl/* + FieldGradient/*'],hardware=['normal actual GPU host wait','injected failed host wait'],field_material=['FieldGradient/*','GradientPreparation/*'],participant_gate='Pending last rebuilt hashes: actual1/3/6 UNIT field and ENGINE material markers required.'),indent=2));return
 os.sched_setaffinity(0,MASK);b=gate()
 try:
  gpuinfo=subprocess.run(['nvidia-smi','--query-gpu=index,name,uuid,driver_version','--format=csv,noheader'],capture_output=True,text=True,timeout=10)
  info=dict(command=gpuinfo.args,returncode=gpuinfo.returncode,stdout=gpuinfo.stdout,stderr=gpuinfo.stderr,scope='Host NVIDIA GPU inventory; test explicitly selects OpenCL GPU ordinal0. Not a substitute for execution proof or guaranteed cross-API ordinal identity.')
 except (OSError,subprocess.TimeoutExpired) as error:info=dict(unavailable=repr(error),scope='Hardware execution proof remains required; host device/driver inventory unavailable.')
 write(OUT/'gpu-sync-host-gpu-inventory.json',info)
 focused(b,'unit',['OpenCLGradient/*','OpenCLRequiredControl/*','FieldGradient/*'],'gpu-sync-focused-unit');gate()
 focused(b,'engine',['GradientPreparation/*'],'gpu-sync-focused-engine');gate()
 hardware(b,False);gate();hardware(b,True);gate()
 field_oracles(b);gate()
 print('Focused, genuine hardware and actual1/3/6 field/material gates complete; root review remains required.',flush=True)
if __name__=='__main__':main()
