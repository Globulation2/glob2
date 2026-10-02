import pathlib,subprocess,json,re,os
root=pathlib.Path(__file__).resolve().parents[2];out=root/'artifacts/memory-optimization'
binaries={'baseline':out/'baseline-source/build/darwin/client/release/src/glob2','candidate':root/'build/darwin/client/release/src/glob2'}
env=os.environ.copy();env.pop('MallocStackLogging',None);env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
results={}
for label,binary in binaries.items():
 directory=out/f'peak-{label}';directory.mkdir(exist_ok=True)
 command=['/usr/bin/time','-l',str(binary),'--run-game','--load-game',str(root/'artifacts/memory-profile/game/checkpoint-45000.game.gz'),'--ticks','45001','--benchmark-warmup','0','--save','final','--output-dir',str(directory)]
 with (directory/'run.log').open('w') as log:subprocess.run(command,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 text=(directory/'run.log').read_text();maximum=int(re.search(r'(\d+)\s+maximum resident set size',text)[1]);results[label]={'maximum_resident_bytes':maximum,'command':command}
 print(label,maximum/1048576,flush=True)
(out/'peak-memory.json').write_text(json.dumps(results,indent=2))
