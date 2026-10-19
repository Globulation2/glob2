from pathlib import Path
import json,random,subprocess,hashlib
from tools import adaptive_gradient_model as model, adaptive_gradient_dataset as data, resource_gradient_model as integer
root=Path(__file__).resolve().parent
actual=json.loads((root/'tools/models/adaptive-gradient.json').read_text())
combined=json.loads((root/'original-combined.json').read_text())
experts=json.loads((root/'original-experts.json').read_text())['experts']
model.validate(actual)
for name in integer.HEADS:
 assert actual[name]==combined[name]
 for identifier,head in actual['family_experts'].items():assert head[name]==experts[data.FAMILIES[int(identifier)]][name]
source='#include "src/map/gradient/AdaptiveGradientModel.h"\n#include <iostream>\nint main(){AdaptiveGradientModel::BaseFeatures b;AdaptiveGradientModel::MovementCounts c;unsigned f;while(std::cin>>f>>b[0]>>b[1]>>b[2]>>b[3]>>b[4]>>b[5]>>b[6]>>c[0]>>c[1]>>c[2])std::cout<<AdaptiveGradientModel::predict(b,c,f)<<"\\n";}'
(root/'parity.cpp').write_text(source)
command=['g++','-std=c++20','-O2','-fsanitize=undefined','-fno-sanitize-recover=undefined',str(root/'parity.cpp'),'-o',str(root/'parity')]
subprocess.run(command,check=True,capture_output=True)
rng=random.Random(1280);vectors=[]
for _ in range(512):
 q=rng.randrange(2**64);vectors.append(([rng.randrange(2000001) for _ in range(7)],[rng.randrange(q+1),rng.randrange(q+1),q]))
for n in [0,1,7,8,31,32,127,128,511,512,4095,4096,999999,1000000,1000001,2**31-1,2**53,2**63-1]:vectors.append(([n]*7,[n,n,n]))
families=[0,1,2,3,4,5,255,2**32-1]
cases=[(f,b,c) for f in families for b,c in vectors]
output=subprocess.check_output([str(root/'parity')],input=''.join(' '.join(map(str,[f,*b,*c]))+'\n' for f,b,c in cases),text=True).splitlines()
assert len(output)==len(cases)
for (family,base,counts),got in zip(cases,output):
 expected_model=experts.get(data.FAMILIES[family],combined) if family<len(data.FAMILIES) else combined
 expected=integer.predict(expected_model,data.prepare(base,counts))
 assert int(got)==expected==(model.predict(actual,base,counts,family)),(family,base,counts,got,expected)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=dict(status='PASS',vectors=len(vectors),cases=len(cases),families=families,ubsan=True,exact_original_combined_and_expert_heads=True,model_sha256=sha(root/'tools/models/adaptive-gradient.json'),header_sha256=sha(root/'src/map/gradient/AdaptiveGradientModel.h'),command=command,original_combined_sha256=sha(root/'original-combined.json'),original_experts_sha256=sha(root/'original-experts.json'))
(root/'PARITY.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
