from pathlib import Path
import json,subprocess,time,os
root=Path.cwd(); base=root/'artifacts/generator-review-example-sweep'
results=[]
for name,width,height,teams in [('swamp',128,256,2),('forts',512,256,4),('even-ground',256,512,4),('forts',512,512,4)]:
    key=f'{name}-{width}x{height}-{teams}'
    args=[str(root/'build/linux/client/release/src/glob2'),'--generator-package',str(base/(name+'.json')),'--generate-map','examples:'+name,'--seed','91','--width',str(width),'--height',str(height),'--teams',str(teams),'--output',str(base/(key+'.map.gz')),'--preview',str(base/(key+'.png')),'--json',str(base/(key+'.report.json'))]
    started=time.monotonic()
    with (base/(key+'.log')).open('w') as log: result=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,env=dict(os.environ,GLOB2_USER_DATA_DIR=str(base/(key+'-profile'))))
    results.append({'command':args,'exitCode':result.returncode,'seconds':round(time.monotonic()-started,3)})
    print(key,result.returncode,flush=True)
(base/'results.json').write_text(json.dumps(results,indent=2)+'\n')
assert all(r['exitCode']==0 for r in results),results
