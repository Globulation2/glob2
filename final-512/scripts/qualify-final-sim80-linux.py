from pathlib import Path
import subprocess,json,time,os,hashlib
c=Path(__file__).parent;root=Path('/home/bradley/glob2-gradient-final-512-validation');build=root/'build/final-pr-release';log=c/'build-sim80.log'
while True:
 s=log.read_text(errors='replace')
 if 'scons: ***' in s:raise RuntimeError('Linux build failed')
 if 'scons: done building targets.' in s:break
 time.sleep(5)
cmd=['python3','test/run_tests.py','--build-dir',str(build),'--binary','engine','--no-display','-j4','--junit',str(c/'final-sim80-engine-linux.xml'),'--artifacts',str(c/'final-sim80-engine-linux')]
for f in ['*Gradient*/*','RuntimeResources/*','SavegameSafety/*','Replay*/*','TurnEngineHarness/*','UnitCustomization/*','UnitCatalog/*','TeamStats*/*','Metric*/*']:cmd+=['--filter',f]
with (c/'final-sim80-engine-linux.log').open('w') as f:subprocess.run(cmd,cwd=root,stdout=f,stderr=subprocess.STDOUT,check=True)
# Integer inference at log/clamp and signed-range corners, using UBSan.
d=c/'integer-model-512';p=d/'model-parity-linux'
subprocess.run(['g++','-std=c++20','-O1','-g0','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root/'src/map/gradient'),str(d/'model-parity.cpp'),'-o',str(p)],check=True)
with (d/'inputs.txt').open('rb') as i,(d/'actual-linux.txt').open('wb') as o:subprocess.run([str(p)],stdin=i,stdout=o,check=True)
assert (d/'actual-linux.txt').read_bytes()==(d/'expected.txt').read_bytes()
print('PASS Linux integer parity8206',flush=True)
rows=json.loads((c/'final-performance-maps/manifest.json').read_text());row=next(r for r in rows if r['format']=='3ffa');spec=row['spec'];out=c/'final-sim80-parity-linux';out.mkdir(exist_ok=True)
for label,threads,probe in [('one',1,False),('four',4,False),('probe',1,True)]:
 d=out/label;d.mkdir(exist_ok=True);cmd=[str(build/'src/glob2'),'game','run','--map-file',str(c/'final-performance-maps/3ffa/map.map.gz'),'--game-seed',str(spec['game_seed']),'--compute-threads',str(threads),'--gpu','off','--ticks','4096','--telemetry','checksums','--save','every:512','--save','final','--output-dir',str(d)]
 for ai in spec['ais']:cmd+=['--player',ai]
 for a in spec['alliances']:cmd+=['--alliance',str(a)]
 if probe:cmd+=['--resource-gradient-probe']
 (d/'command.json').write_text(json.dumps(cmd,indent=2))
 with (d/'run.log').open('w') as f:subprocess.run(cmd,cwd=root,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=600)
 print(label,'complete',flush=True)
for name in ['world.checksums','snapshot.checksums','game.replay.checksums']:
 ref=(out/'one'/name).read_bytes()
 for label in ['four','probe']:assert (out/label/name).read_bytes()==ref,(name,label)
print('PASS Linux 1vs4threads+probeon/off per-tick parity',flush=True)
# Real released173 input, then save174 and continue once more.
for label,source,ticks in [('released173',c/'released173-save/final.game.gz',1024),('resaved174',c/'released173/final.game.gz',1280)]:
 d=c/label;d.mkdir(exist_ok=True);cmd=[str(build/'src/glob2'),'game','run','--load-game',str(source),'--ticks',str(ticks),'--gpu','off','--compute-threads','1','--save','final','--telemetry','checksums','--output-dir',str(d)]
 with (d/'run.log').open('w') as f:subprocess.run(cmd,cwd=root,stdout=f,stderr=subprocess.STDOUT,check=True)
print('PASS real173load+174resave continuation',flush=True)
(c/'qualification-complete-sim80.json').write_text(json.dumps({'source':'ca6365e83900e2668ff2348044a89ba3b2d70c43','status':'passed'}))
