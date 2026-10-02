from pathlib import Path
import importlib.util,subprocess,json
root=Path(__file__).resolve().parents[2];out=root/'artifacts/memory-optimization'
spec=importlib.util.spec_from_file_location('benchmark',root/'tools/memory_benchmark.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
binaries={}
for label,src in [('baseline',out/'baseline-source'),('candidate',root)]:
 binary=out/f'farming-{label}';binaries[label]=binary
 command=['c++','-O3','-std=c++17','-I'+str(src/'src/ai/maxima'),str(out/'farming_probe.cpp'),str(src/'src/ai/maxima/AIMaximaFarming.cpp'),'-o',str(binary)]
 subprocess.run(command,check=True)
results={}
for mode in range(3):
 pairs=[]
 for pair in range(21):
  data={}
  for label in (['baseline','candidate'] if pair%2==0 else ['candidate','baseline']):
   cpu,digest=subprocess.check_output([str(binaries[label]),str(mode)],text=True).split();data[label]={'cpu':float(cpu),'digest':digest}
  assert data['baseline']['digest']==data['candidate']['digest'];pairs.append(data)
  confidence=module.confidence([p['candidate']['cpu']/p['baseline']['cpu'] for p in pairs])
  if pair>=6 and confidence['upper_95_percent']<=2:break
 results[mode]={'pairs':pairs,'cpu':confidence,'passed':confidence['upper_95_percent']<=2}
 print(mode,confidence,flush=True)
(out/'kernel-comparison.json').write_text(json.dumps(results,indent=2))
assert all(v['passed'] for v in results.values())
