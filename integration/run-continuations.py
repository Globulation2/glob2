"""Local functional evidence only; development builds are not performance evidence."""
from pathlib import Path
import json, os, subprocess, time
ROOT=Path.cwd()
OUT=ROOT/'artifacts/gradient-merge'
BIN=ROOT/'build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2'
SAVE=ROOT/'artifacts/gradient-games/vulkan-frozen8-checksum/final.game.gz'
rows=[]
def run(label,arguments,env=None):
    command=[str(BIN),*map(str,arguments)]
    started=time.monotonic()
    with (OUT/(label+'.log')).open('w') as log:
        result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,env={**os.environ,**(env or {})})
    row=dict(label=label,command=command,environment=env or {},exit=result.returncode,seconds=time.monotonic()-started)
    rows.append(row)
    (OUT/'continuation-runs.json').write_text(json.dumps(rows,indent=2)+'\n')
    print(label,result.returncode,flush=True)
    return row
baseline=None
for backend,algorithm in [('scalar','buckets'),('opencl','frozen8'),('vulkan','frozen8'),('vulkan','jacobi')]:
    for count in [1,2,4,8]:
        label=f'continuation-{backend}-{algorithm}-{count}'
        directory=OUT/label
        row=run(label,['game','run','--load-game',SAVE,'--ticks','512','--compute-threads',count,'--gradient-backend',backend,'--gradient-algorithm',algorithm,'--gradient-verify','on','--telemetry','checksums','--telemetry','gradient-stats','--save','final','--output-dir',directory])
        if row['exit']==0:
            result=json.loads((directory/'result.json').read_text())
            checksum=(directory/'world.checksums').read_bytes()
            if baseline is None:baseline=checksum
            row.update(checksums_match=checksum==baseline,status=result['status'],gpu_fields=result['gradient_gpu_fields'],verified=result['gradient_verified'],fallbacks=result['gradient_cpu_fallbacks'],finalChecksum=result['finalChecksum'])
        (OUT/'continuation-runs.json').write_text(json.dumps(rows,indent=2)+'\n')
