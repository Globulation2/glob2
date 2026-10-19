#!/usr/bin/env python3
"""Terminal-only isolated research fits; never emits/deploys coefficients."""
import pathlib,json,hashlib,os,fcntl,subprocess,datetime,sys,traceback
D=pathlib.Path(__file__).resolve().parent;LOCK=(D/'remote-fit.lock').open('a')
try:fcntl.flock(LOCK,fcntl.LOCK_EX|fcntl.LOCK_NB)
except BlockingIOError:raise SystemExit('Another exact-input fit process owns the lock')
os.nice(10);os.sched_setaffinity(0,{0});os.environ.update(OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1',MKL_NUM_THREADS='1',NUMEXPR_NUM_THREADS='1')
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def write(p,value):
 temp=p.with_suffix(p.suffix+'.tmp');temp.write_text(json.dumps(value,indent=2)+'\n');os.replace(temp,p)
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
inputs=json.loads((D/'terminal-inputs.json').read_text());fingerprint=sha(D/'terminal-inputs.json');status=dict(status='VERIFYING',pid=os.getpid(),updated_utc=now(),input_sha256=fingerprint,stages={},prospective_or_production_promotion=False)
state=D/'fit-status.json'
try:
 for relative,h in inputs['files'].items():
  if sha(D/relative)!=h:raise ValueError('Immutable fitting input changed: '+relative)
 if inputs['slot_counts'].get('completed',0)+inputs['slot_counts'].get('user-censored',0)!=inputs['expected_slots']:raise ValueError('Whole declared terminal accounting required')
 
 for cohort in inputs['cohorts']:
  if cohort['expected']!=128 or sum(cohort['counts'].get(k,0) for k in ('completed','user-censored'))!=128:raise ValueError('Each frozen role requires128 terminal slots')
 S=D/'analysis-tool-source';sys.path.insert(0,str(S))
 from tools import adaptive_gradient_spatial as spatial,adaptive_gradient_dataset as base
 from tools.adaptive_gradient_model import age_demand_support
 manifests=[D/p for p in inputs['manifests']];rows=spatial.load(manifests);known=base.eligible(rows);aug=spatial.eligible(rows)
 write(D/'pre-fit-coverage.json',dict(rows=len(rows),generic_known=len(known),spatial_known=len(aug),coverage=base.coverage(known),age_demand=age_demand_support(known),expected_families=inputs['expected_families'],campaign_scope=inputs['campaign_scope']))
 import numpy,sklearn,scipy
 status.update(status='FITTING',affinity=sorted(os.sched_getaffinity(0)),python=sys.version,dependencies=dict(numpy=numpy.__version__,sklearn=sklearn.__version__,scipy=scipy.__version__));write(state,status)
 common=[part for p in manifests for part in ['--manifest',str(p)]];registry=D/'map-splits.json'
 stages=[('generic12',[sys.executable,str(S/'tools/adaptive_gradient_model.py'),'fit',*common,'--split-registry',str(registry),'--expected-families',','.join(inputs['expected_families']),'--collector-contract',spatial.CONTRACT_ID,'--output',str(D/'generic-research.json')],D/'generic-research.json'),('spatial',[sys.executable,str(S/'tools/adaptive_gradient_spatial.py'),*common,'--split-registry',str(registry),'--output',str(D/'spatial-research.json')],D/'spatial-research.json')]
 for name,command,output in stages:
  receipt=D/(name+'-stage.json')
  if receipt.exists():
   old=json.loads(receipt.read_text())
   if old.get('input_sha256')!=fingerprint:raise ValueError('Fit receipt belongs to different inputs')
   if old.get('returncode')==0 and output.exists() and sha(output)==old.get('output_sha256'):status['stages'][name]=old;continue
   raise ValueError('Prior incomplete/failed fit needs review; refusing duplicate automatic fitting: '+name)
  row=dict(started_utc=now(),input_sha256=fingerprint,command=command);write(receipt,row);status['stages'][name]=row;write(state,status)
  with (D/(name+'.log')).open('w') as log:result=subprocess.run(command,cwd=S,stdout=log,stderr=subprocess.STDOUT)
  row.update(returncode=result.returncode,finished_utc=now(),output_sha256=sha(output) if result.returncode==0 else None);write(receipt,row);status['stages'][name]=row;status['updated_utc']=now();write(state,status)
 status.update(status='COMPLETE' if all(s['returncode']==0 for s in status['stages'].values()) else 'COMPLETE-WITH-FIT-BLOCKER',updated_utc=now());write(state,status)
except Exception:
 status.update(status='FAILED-REQUIRES-REVIEW',error=traceback.format_exc(),updated_utc=now());write(state,status);raise
