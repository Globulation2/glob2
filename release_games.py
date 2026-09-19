import concurrent.futures,subprocess,os,json
root=os.path.abspath('artifacts/portage-lakes');b=root+'/final-glob2'
ais=['nicowar','cortex','cabino','maxima']
specs=[('release-compact-1',64,64,2,1,ais[:2]),('release-compact-41',64,64,2,41,ais[2:]),('release-narrow-17',64,256,4,17,ais),('release-full-1',256,256,4,1,ais),('release-full-17',256,256,4,17,ais),('release-full-41',256,256,4,41,ais)]
for name,w,h,t,seed,roster in specs:
 p=root+'/'+name
 with open(p+'-generation.log','w') as f:r=subprocess.run([b,'--generate-map','portage-lakes','--seed',str(seed),'--width',str(w),'--height',str(h),'--teams',str(t),'--set','workers=8','--output',p+'.map','--preview',p+'.png','--json',p+'.json'],stdout=f,stderr=f)
 if r.returncode:raise RuntimeError(name+' generation failed')
jobs=[]
for name,w,h,t,seed,roster in specs:
 rotations=t if seed==1 or w==64 else 1
 for rotation in range(rotations):jobs.append((name,roster[rotation:]+roster[:rotation],rotation))
def run(job):
 name,roster,rotation=job;prefix=root+'/'+name+'-r'+str(rotation)
 cmd=[b,'--run-game','--map-file',root+'/'+name+'.map','--game-seed','2','--ticks','30000','--telemetry','team-timeline','--save','initial','--save','final','--output-dir',prefix]
 if name=='release-full-1' and rotation==0:cmd+=['--telemetry','checksums']
 for ai in roster:cmd+=['--player',ai]
 with open(prefix+'.log','w') as f:r=subprocess.run(cmd,env=dict(os.environ,SDL_VIDEODRIVER='dummy'),stdout=f,stderr=f)
 return name,rotation,r.returncode
with concurrent.futures.ThreadPoolExecutor(3) as pool:
 for r in pool.map(run,jobs):print(r,flush=True)
