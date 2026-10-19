"""Emit and qualify a filtered artifact candidate; never writes canonical files."""
from pathlib import Path
import json
import hashlib
import shutil
import subprocess
import sys
import random
from types import SimpleNamespace

root=Path(__file__).resolve().parent
source=root/'analysis-source'
sys.path.insert(0,str(source))
from tools import adaptive_gradient_model as models,adaptive_gradient_dataset as data,adaptive_gradient_codegen as codegen,resource_gradient_model as integer
out=root/'staged';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
derived=json.loads((root/'DERIVATION.json').read_text())
original=Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-005/cumulative-1280-research')
old_inputs=json.loads((original/'terminal-inputs.json').read_text())
manifest_names=[str(Path(item['path']).relative_to(root)) for item in derived['manifests']]
files={name:sha(root/name) for name in manifest_names}
files['map-splits.json']=sha(root/'map-splits.json')
identity={str(p.relative_to(source)):sha(p) for p in source.rglob('*.py') if '__pycache__' not in p.parts}
# Retain the existing input-receipt key for older compactor compatibility; contents
# explicitly identify the current fitter and policy, rather than an old A16 fit.
(root/'ANALYSIS-SOURCE-A16-FROZEN.json').write_text(json.dumps(dict(role='current-filtered-fit-tooling',threshold=24000,minimum=32,files=identity),indent=2)+'\n')
files['ANALYSIS-SOURCE-A16-FROZEN.json']=sha(root/'ANALYSIS-SOURCE-A16-FROZEN.json')
inputs=dict(schema=1,fit_only=True,expected_slots=1280,slot_counts=old_inputs['slot_counts'],manifests=manifest_names,files=files,original_input_sha256=sha(original/'terminal-inputs.json'),exclusions_sha256=sha(root/'EXCLUSIONS.json'),eligible_before=287045,excluded_eligible_guard_rows=49925,eligible_after=237120)
(root/'FILTERED-TRAINING-INPUTS.json').write_text(json.dumps(inputs,indent=2)+'\n')
models.emit(SimpleNamespace(model=str(root/'filtered-family-research.json'),contract=data.CONTRACT_ID,output=str(out/'deployment.json'),header=str(out/'AdaptiveGradientModel.h'),threshold_milli=24000,compact=True,training_inputs=str(root/'FILTERED-TRAINING-INPUTS.json'),evidence_url=None))
deployment=json.loads((out/'deployment.json').read_text())
deployment['provenance']['row_filter']=dict(id='eligible-guard-exact-nonpainting-ai-v1',criterion='Known movement Guard episodes on exact nonpainting AI seats; not a zero-goal filter.',nonpainting_ais=['cabino','castor','cortex','econo','nicowar','numbi'],eligible_before=287045,excluded=49925,eligible_after=237120,retained_guard=19791,original_input_sha256=inputs['original_input_sha256'],exclusions_sha256=inputs['exclusions_sha256'],original_collection_minimum_interval_ticks=8,assumption='Pure generated initial maps; no external orders. Maxima/Warrush zero-goal rows retained as unknown painted presence.')
models.validate(deployment)
(out/'deployment.json').write_text(json.dumps(deployment,sort_keys=True,indent=2)+'\n')
assert (out/'AdaptiveGradientModel.h').read_text()==codegen.render(deployment)
raw=json.loads((root/'filtered-family-research.json').read_text())
for head in integer.HEADS:
 assert deployment[head]==raw[head]
 for key,expert in deployment['family_experts'].items():assert expert[head]==raw['family_experts'][key][head]
cpp='#include "AdaptiveGradientModel.h"\n#include <iostream>\nint main(){AdaptiveGradientModel::BaseFeatures b;AdaptiveGradientModel::MovementCounts c;unsigned f;while(std::cin>>f>>b[0]>>b[1]>>b[2]>>b[3]>>b[4]>>b[5]>>b[6]>>c[0]>>c[1]>>c[2])std::cout<<AdaptiveGradientModel::predict(b,c,f)<<"\\n";}'
(out/'parity.cpp').write_text(cpp)
command=['g++','-std=c++20','-O2','-fsanitize=undefined','-fno-sanitize-recover=undefined',str(out/'parity.cpp'),'-o',str(out/'parity')]
subprocess.run(command,check=True,capture_output=True)
rng=random.Random(1280);vectors=[]
for _ in range(512):
 q=rng.randrange(2**64);vectors.append(([rng.randrange(2000001) for _ in range(7)],[rng.randrange(q+1),rng.randrange(q+1),q]))
for number in [0,1,7,8,31,32,127,128,511,512,4095,4096,999999,1000000,1000001,2**31-1,2**53,2**63-1]:vectors.append(([number]*7,[number]*3))
families=[0,1,2,3,4,5,255,2**32-1];cases=[(family,base,counts) for family in families for base,counts in vectors]
actual=subprocess.check_output([str(out/'parity')],input=''.join(' '.join(map(str,[family,*base,*counts]))+'\n' for family,base,counts in cases),text=True).splitlines()
combined,experts=models.inference_views(raw)
for (family,base,counts),value in zip(cases,actual):
 expected=integer.predict(experts.get(family,combined),data.prepare(base,counts))
 assert int(value)==expected==models.predict(deployment,base,counts,family)
assert len(actual)==len(cases)==4240
report=dict(status='PASS-STAGED-NUMERIC-CANDIDATE',model_sha256=sha(out/'deployment.json'),header_sha256=sha(out/'AdaptiveGradientModel.h'),raw_fit_sha256=sha(root/'filtered-family-research.json'),training_input_sha256=sha(root/'FILTERED-TRAINING-INPUTS.json'),exclusions_sha256=sha(root/'EXCLUSIONS.json'),cases=len(cases),families=families,ubsan=True,bounds_all_heads=True,threshold_milli=24000,minimum_interval_ticks=32,cap_ticks=1024,combined_and_expert_heads_exact_raw_fit=True,canonical_modified=False,command=command)
(out/'NUMERIC-QUALIFICATION.json').write_text(json.dumps(report,indent=2)+'\n')
# Run permanent model regression tests against this actual deployment artifact.
validation=root/'model-validation-source'
shutil.copytree(source,validation)
shutil.copy2(out/'deployment.json',validation/'tools/models/adaptive-gradient.json')
with (out/'model-tests.log').open('w') as log:
 result=subprocess.run([sys.executable,'-m','unittest','tools.test_adaptive_gradient_model','-v'],cwd=validation,stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0
report['permanent_model_tests_pass']=True
report['model_tests_log_sha256']=sha(out/'model-tests.log')
(out/'NUMERIC-QUALIFICATION.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
