import json,subprocess,sys
from pathlib import Path
root=Path.cwd(); candidate=root/'artifacts/gpu-offload/candidates/786feaa6a';rows=[]
for plan in ('frozen8','frozen16','jacobi4'):
 for check in (1,8,32):
  ident=plan+'-check'+str(check);config=candidate/'plan-check-diagnose'/(ident+'.json');output=root/('artifacts/gpu-offload/candidate-786feaa6a-plan-check-'+ident+'-diagnose');command=[sys.executable,'test/benchmark_gpu_offload.py',str(config),'--output',str(output),'--lock',str(root.parent/'gpu-offload-resource.lock'),'--stage','diagnose'];result=subprocess.run(command,cwd=root)
  if result.returncode:raise SystemExit(result.returncode)
  summary=json.loads((output/'summary.json').read_text());metrics=summary['scenarios']['512-open-early']['candidate-gpu-'+ident]['metrics'];row={'plan':plan,'check_interval':check,'output':str(output),'qualifying_evidence':False,'stage':'diagnose','cpu_ratio':metrics['cpu_per_tick']['ratio'],'wall_ratio':metrics['wall_per_tick']['ratio'],'tick_p99_ratio':metrics['tick_p99']['ratio']};rows.append(row);print(json.dumps(row),flush=True);(candidate/'plan-check-diagnose'/'wave-summary.json').write_text(json.dumps(rows,indent=2)+'\n')
