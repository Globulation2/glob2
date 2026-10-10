from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import os, subprocess, json
root=Path.cwd()
binary=root/'artifacts/discord-focused-gallery'
out=root/'artifacts/discord-final-validation'
out.mkdir(exist_ok=True)
cases=[('desktop',1280,800,'desktop'),('phone-portrait',320,568,'compact'),('phone-landscape',568,320,'compact')]
def run(case):
 name,w,h,mode=case
 profile=out/name
 profile.mkdir(exist_ok=False)
 env=os.environ.copy()
 env.update(GLOB2_USER_DATA_DIR=str(profile),SDL_VIDEODRIVER='dummy',SDL_RENDER_DRIVER='software')
 cmd=[str(binary),str(w),str(h),mode]
 with (out/(name+'.log')).open('w') as log:
  result=subprocess.run(cmd,env=env,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=300)
 captures=list(profile.glob('*.bmp'))
 print(json.dumps({'case':name,'exit':result.returncode,'captures':len(captures),'discord':(profile/'discord-community.bmp').exists()}),flush=True)
 return result.returncode
with ThreadPoolExecutor(max_workers=3) as pool:
 results=list(pool.map(run,cases))
assert not any(results),results
