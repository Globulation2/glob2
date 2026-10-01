from pathlib import Path
import os,subprocess,json,hashlib
p=Path('artifacts/render-profile').resolve()
checkpoint=p/'ai-match-eight/checkpoint-20000.game.gz'
assert checkpoint.exists()
env=os.environ.copy();env.update(GLOB2_USER_DATA_DIR=str(p/'map-profile'),GLOB2_UI_SCALE='1',SDL_AUDIODRIVER='dummy',GLOB2_BENCH_FLAT='1',GLOB2_BENCH_FULL_MAP='1',GLOB2_BENCH_GAME=str(checkpoint),GLOB2_BENCH_MIN_UNITS='3000',GLOB2_BENCH_AI_TICKS='100',GLOB2_BENCH_BARS='1',GLOB2_BENCH_FRAMES='30')
results=[]
for label,binary,sweep in [('ai-reference',p/'ai-reference-benchmark',False),('ai-optimized',Path('build/darwin/client/release/test/torus-render-benchmark').resolve(),False),('ai-sweep',Path('build/darwin/client/release/test/torus-render-benchmark').resolve(),True)]:
 e=env.copy()
 e.pop('GLOB2_BENCH_CAMERA_SWEEP',None);e.pop('GLOB2_BENCH_NATIVE_CLOUD_DETAIL',None);e.pop('GLOB2_BENCH_MODE',None)
 if label=='ai-reference':e['GLOB2_BENCH_NATIVE_CLOUD_DETAIL']='1'
 if sweep:e['GLOB2_BENCH_CAMERA_SWEEP']='1';e['GLOB2_BENCH_FRAMES']='120'
 e['GLOB2_BENCH_CAPTURE']=str(p/(label+'.ppm'))
 command=[str(binary),'-g','-F','-m','-s','1024x600']
 with (p/(label+'.log')).open('w') as out: result=subprocess.run(command,env=e,stdout=out,stderr=subprocess.STDOUT)
 lines=[x for x in (p/(label+'.log')).read_text().splitlines() if any(k in x for k in ('GPU=','AI checkpoint','flat zoom','median=','draw_calls=','simulation_checksum='))]
 print(label,result.returncode,'\n'+'\n'.join(lines),flush=True)
 results.append(dict(label=label,exitCode=result.returncode,command=command,environment={k:v for k,v in e.items() if k.startswith('GLOB2_') or k=='SDL_AUDIODRIVER'},binarySHA256=hashlib.sha256(binary.read_bytes()).hexdigest(),measurements=lines))
 (p/'ai-comparison.json').write_text(json.dumps(dict(checkpointSHA256=hashlib.sha256(checkpoint.read_bytes()).hexdigest(),results=results),indent=2)+'\n')
 assert result.returncode==0
