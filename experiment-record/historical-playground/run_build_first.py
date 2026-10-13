#!/usr/bin/env python3
"""Compile the complete frozen revision set, then run fresh fixed-affinity measurements."""
from pathlib import Path
import datetime,hashlib,json,subprocess,sys,time
ROOT=Path(__file__).resolve().parent
config=json.loads((ROOT/'experiment.json').read_text())
def status(state,**fields):
 data=dict(state=state,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),**fields)
 tmp=ROOT/'workflow-progress.tmp';tmp.write_text(json.dumps(data,indent=2)+'\n');tmp.replace(ROOT/'workflow-progress.json')
def check_binaries():
 records=[]
 for snap in config['snapshots']:
  folder=ROOT/'snapshots'/(snap['sample_date']+'-'+snap['commit'][:9])
  rec=json.loads((folder/'build.json').read_text());p=Path(rec['binary'])
  with p.open('rb') as stream:actual=hashlib.file_digest(stream,'sha256').hexdigest()
  if actual!=rec['binary_sha256']:raise RuntimeError(f'Binary hash mismatch: {p}')
  records.append(dict(sample_date=snap['sample_date'],commit=snap['commit'],binary=rec['binary'],binary_sha256=actual))
 (ROOT/'prebuilt-binaries.json').write_text(json.dumps(dict(validated_at=datetime.datetime.now(datetime.timezone.utc).isoformat(),binaries=records),indent=2)+'\n')
try:
 status('compiling_all',build_jobs=32,build_cpu_affinity=list(range(32)))
 for index,snap in enumerate(config['snapshots']):
  print('PREPARE',index+1,len(config['snapshots']),snap['sample_date'],flush=True)
  status('compiling_all',snapshot=snap['sample_date'],index=index+1,total=len(config['snapshots']),build_jobs=32)
  subprocess.run([sys.executable,str(ROOT/'run_experiment.py'),'--build-only','--only-date',snap['sample_date'],'--build-jobs','32'],check=True)
 check_binaries()
 status('measuring',target_runs=config['target_runs']);print('ALL BINARIES READY; STARTING FRESH MEASUREMENTS',flush=True)
 with (ROOT/'experiment-build-first.log').open('a') as log:
  subprocess.run([sys.executable,str(ROOT/'run_experiment.py'),'--require-prebuilt'],stdout=log,stderr=subprocess.STDOUT,check=True)
 status('complete',target_runs=config['target_runs'])
except Exception as error:
 status('failed',error=str(error));raise
