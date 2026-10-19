"""One explicit cumulative lean12 refit after terminal preservation; no promotion."""
from pathlib import Path
import json,subprocess,os,hashlib,datetime,shutil,sys
R=Path('/home/bradley/glob2/artifacts/gradient-contradictions-20261018/postprocess-cumulative-round002');PY='/home/bradley/glob2/artifacts/gradient-coverage-72d2f99/training-venv/bin/python'
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert (R.parent/'round-002-candidate-readonly/experiment.json').exists()
with (R/'final-seal.log').open('w') as log:subprocess.run([PY,str(R/'seal-terminal.py')],stdout=log,stderr=subprocess.STDOUT,check=True)
paths=json.loads((R/'cumulative-manifests-terminal256.json').read_text());assert len(paths)>6,'Candidate cohort missing'
registry=R/'map-split-registry.json';original=R.parent/'postprocess-f65/map-split-registry.json';assert h(original)=='15c6670f4e4e2e92faf3709f0834c6015e73de2b86f6ae69c1956f4334680508'
if not registry.exists():shutil.copy2(original,registry)
before=json.loads(registry.read_text());base=json.loads(original.read_text());assert all(before['assignments'].get(k)==v for k,v in base['assignments'].items())
argv=[PY,str(R/'source/tools/resource_gradient_model.py'),'fit-candidate','--feature-set','goal-stall-v1','--split-registry',str(registry),'--output',str(R/'cumulative-lean12-candidate.json')]
for p in paths:argv+=['--manifest',p]
env=dict(os.environ,OPENBLAS_NUM_THREADS='1',OMP_NUM_THREADS='1',MKL_NUM_THREADS='1')
proof={'argv':argv,'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_revision':'2d4dc8e9e1051a80b98fb9c73cf2e7e6610173ec','source_archive_sha256':h(R/'source-tools-2d4.tar.gz'),'registry_before_sha256':h(registry),'manifests':[{'path':p,'sha256':h(Path(p))} for p in paths],'promotion':False}
assert not (R/'FIT-COMMAND.json').exists(),'Do not launch a duplicate fit'
(R/'FIT-COMMAND.json').write_text(json.dumps(proof,indent=2)+'\n')
with (R/'fit.log').open('w') as log:
 p=subprocess.Popen(argv,cwd=R/'source',stdout=log,stderr=subprocess.STDOUT,env=env);(R/'FIT-PID.json').write_text(json.dumps({'pid':p.pid,'supervisor':os.getpid(),'command_receipt':str(R/'FIT-COMMAND.json')})+'\n');code=p.wait()
assert code==0,('Fit failed',code)
after=json.loads(registry.read_text());assert all(after['assignments'].get(k)==v for k,v in before['assignments'].items()),'Old heldout/fold assignment changed'
model=json.loads((R/'cumulative-lean12-candidate.json').read_text());summary={'status':'passed-research-fit-not-promoted','model_sha256':h(R/'cumulative-lean12-candidate.json'),'registry_after_sha256':h(registry),'preserved_assignments':len(before['assignments']),'final_assignments':len(after['assignments']),'training_games':model['provenance']['training_games'],'eligible_movement_episodes':model['provenance']['eligible_movement_episodes'],'holdout':model['provenance']['holdout'],'cross_validation':model['provenance']['cross_validation'],'finished_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
(R/'FIT-COMPLETE.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary),flush=True)
