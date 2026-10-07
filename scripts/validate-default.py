import sys,json,subprocess
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'test'))
from benchmark_parallel_compute import execute,digest
from compare_save_continuation import compare
binary=Path(sys.argv[1]).resolve();out=root/'artifacts/greedy-fetch/default-validation';out.mkdir(exist_ok=True)
fixture=root/'artifacts/greedy-fetch/maps/coral-256-1606.map.gz'
base=['--map-file',str(fixture),'--game-seed','51606',*sum((['--player',p] for p in ['numbi','castor','numbi','castor']),[]),'--telemetry','checksums','--compute-experiments','ai']
results={}
for mode in ['greedy','round-trip']:
 common=base+(['--experiment','round-trip-resource-fetching'] if mode=='round-trip' else [])
 dest=out/mode;dest.mkdir(exist_ok=True)
 rows=[]
 for workers in [0,1,2,4,8]:
  row=execute(binary,[*common,'--ticks','1024','--compute-threads',str(max(1,workers)),'--gradient-workers',str(workers)],dest/f'workers-{workers}',cwd=root);rows.append(row)
  hash_=digest(dest/f'workers-{workers}'/'game.replay.checksums')
  if workers==0:expected=hash_
  assert hash_==expected,(mode,workers,hash_,expected)
  print('checksum matches',mode,workers,hash_,flush=True)
 warm=execute(binary,[*common,'--ticks','512','--compute-threads','1','--gradient-workers','0','--save','final'],dest/'checkpoint',cwd=root)
 resumed=execute(binary,['--load-game',str(dest/'checkpoint/final.game.gz'),'--ticks','1024','--compute-threads','4','--gradient-workers','4','--telemetry','checksums','--compute-experiments','ai'],dest/'resumed',cwd=root)
 assert ('round-trip-resource-fetching' in resumed['result']['resolved']['experiments']) == (mode=='round-trip')
 count=compare(dest/'workers-0/game.replay.checksums',dest/'resumed/game.replay.checksums');assert count==512
 print('save continuation matches',mode,count,flush=True)
 old=root/'artifacts/greedy-fetch/binaries'/('glob2-timing-macos' if sys.platform=='darwin' else 'glob2-timing-linux')
 legacy=execute(old,[*base,*(['--experiment','greedy-resource-fetching'] if mode=='greedy' else []),'--ticks','1024','--compute-threads','1','--gradient-workers','0'],dest/'legacy',cwd=root)
 same=compare(dest/'legacy/game.replay.checksums',dest/'workers-0/game.replay.checksums');assert same==1024
 print('previous algorithm matches',mode,same,flush=True)
 if mode=='greedy':
  oldWarm=execute(old,[*base,'--experiment','greedy-resource-fetching','--ticks','512','--compute-threads','1','--gradient-workers','0','--save','final'],dest/'legacy-checkpoint',cwd=root)
  legacyResume=execute(binary,['--load-game',str(dest/'legacy-checkpoint/final.game.gz'),'--ticks','1024','--compute-threads','1','--gradient-workers','0','--telemetry','checksums','--compute-experiments','ai'],dest/'legacy-resumed',cwd=root)
  assert 'round-trip-resource-fetching' not in legacyResume['result']['resolved']['experiments']
  assert compare(dest/'workers-0/game.replay.checksums',dest/'legacy-resumed/game.replay.checksums')==512
  print('legacy greedy save continuation matches',512,flush=True)
 results[mode]={'checksum_sha256':expected,'rows':rows,'checkpoint':warm,'resumed':resumed,'continuation_matching_ticks':count,'legacy':legacy,'legacy_matching_ticks':same}
(out/'manifest.json').write_text(json.dumps({'binary_sha256':digest(binary),'source':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'modes':results},indent=2))
