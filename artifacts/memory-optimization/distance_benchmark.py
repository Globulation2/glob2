import pathlib,importlib.util,json
root=pathlib.Path(__file__).resolve().parents[2];out=root/'artifacts/memory-optimization';spec=importlib.util.spec_from_file_location('b',root/'tools/memory_benchmark.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
pairs=[]
for pair in range(21):
 dirs={v:out/f'distance-pair-{pair}-{v}' for v in ('baseline','candidate')}
 for d in dirs.values():d.mkdir(exist_ok=True)
 commands={v:[str(out/f'distance-{v}')] for v in dirs};order=tuple(dirs) if pair%2==0 else tuple(reversed(dirs))
 m.run_pair(commands,dirs,order,{},.025);values={}
 for label,d in dirs.items():
  cpu,digest=(d/'run.log').read_text().split();values[label]={'cpu':float(cpu),'digest':digest}
 assert values['baseline']['digest']==values['candidate']['digest'];pairs.append(values)
 c=m.confidence([p['candidate']['cpu']/p['baseline']['cpu'] for p in pairs]);print(pair+1,c,flush=True)
 if pair>=6 and c['upper_95_percent']<=2:break
(out/'distance-comparison.json').write_text(json.dumps({'pairs':pairs,'cpu':c,'passed':c['upper_95_percent']<=2},indent=2))
