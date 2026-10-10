import os,subprocess
from pathlib import Path
p=Path(__file__).resolve().parent
for seed in [*range(10),*range(1791590193,1791590198)]:
 env=dict(os.environ,LD_PRELOAD=str(p/'fixed-time.so'),TEST_TIME_SEED=str(seed))
 r=subprocess.run(['build/native-coverage-torus/test/glob2-engine-tests','--test-case=placement maintenance regressions'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 (p/f'fixed-seed-{seed}.log').write_text(r.stdout)
 print(seed,r.returncode,[s for s in r.stdout.splitlines() if 'assertions:' in s or 'parcel resources=' in s],flush=True)
 if r.returncode: raise SystemExit(r.returncode)
