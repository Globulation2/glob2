import sys,json,subprocess,hashlib
from pathlib import Path
root=Path.cwd();sys.path.insert(0,str(root/'test'))
from benchmark_parallel_compute import execute,digest
from compare_save_continuation import compare
binary=Path(sys.argv[1]).resolve();out=root/'artifacts/greedy-fetch/validation';out.mkdir(exist_ok=True)
fixture=root/'artifacts/greedy-fetch/maps/coral-256-1606.map.gz'
common=['--map-file',str(fixture),'--game-seed','51606',*sum((['--player',p] for p in ['numbi','castor','numbi','castor']),[]),'--experiment','greedy-resource-fetching','--telemetry','checksums','--compute-experiments','ai']
rows=[]
for workers in [0,1,2,4,8]:
 row=execute(binary,[*common,'--ticks','1024','--compute-threads',str(max(1,workers)),'--gradient-workers',str(workers)],out/f'workers-{workers}',cwd=root);rows.append(row)
 hash_=digest(out/f'workers-{workers}'/'game.replay.checksums')
 if workers==0:expected=hash_
 assert hash_==expected,(workers,hash_,expected)
 print('checksum matches',workers,hash_,flush=True)
warm=execute(binary,[*common,'--ticks','512','--compute-threads','1','--gradient-workers','0','--save','final'],out/'checkpoint',cwd=root)
resumed=execute(binary,['--load-game',str(out/'checkpoint/final.game.gz'),'--ticks','1024','--compute-threads','4','--gradient-workers','4','--telemetry','checksums','--compute-experiments','ai'],out/'resumed',cwd=root)
assert 'greedy-resource-fetching' in resumed['result']['resolved']['experiments']
count=compare(out/'workers-0/game.replay.checksums',out/'resumed/game.replay.checksums');assert count==512
print('save continuation matches',count,flush=True)
(out/'manifest.json').write_text(json.dumps({'binary_sha256':digest(binary),'source':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'checksum_sha256':expected,'rows':rows,'checkpoint':warm,'resumed':resumed,'continuation_matching_ticks':count},indent=2))
