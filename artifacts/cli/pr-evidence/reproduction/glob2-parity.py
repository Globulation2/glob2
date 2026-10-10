from pathlib import Path
import subprocess,os,json,hashlib
root=Path.cwd(); out=root/'artifacts/cli/parity-reviewed';out.mkdir(exist_ok=True)
base=root/'artifacts/cli/glob2-baseline';new=root/'build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2'
def run(binary,name,args):
 env=dict(os.environ,GLOB2_USER_DIR=str(out/name/'profile'),GLOB2_USER_DATA_DIR=str(out/name/'profile'),SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
 result=subprocess.run([str(binary),*map(str,args)],cwd=root,env=env,capture_output=True,text=True,timeout=180)
 (out/(name+'.log')).write_text(result.stdout+result.stderr)
 assert result.returncode==0,(name,result.returncode,result.stderr)
 return result.stdout
catalogOld=json.loads(run(base,'catalog-old',['--headless-catalog']));catalogNew=json.loads(run(new,'catalog-new',['info','catalog','--format','json']));assert catalogOld==catalogNew
for name,binary,args in [('old',base,['--generate-map','--generator','river','--map-seed','713']),('new',new,['map','study','river','--seed','713'])]:
 run(binary,'map-'+name,args+['--output-dir']+[out/('map-'+name)]+[('--param' if name=='old' else '--set'),'teams=2']+(['--write-map','true'] if name=='old' else ['--write-map']))
assert (out/'map-old/map-r0.map.gz').read_bytes()==(out/'map-new/map-r0.map.gz').read_bytes()
for name,binary,args in [('old',base,['--run-game','--output-dir']),('new',new,['game','run','--output-dir'])]:
 run(binary,'game-'+name,args+[out/('game-'+name),'--map-file',out/'map-old/map-r0.map.gz','--game-seed','713','--player','castor','--player','cortex','--ticks','64','--compute-threads','1','--telemetry','checksums','--save','every:32','--save','final']+(['--replay','true'] if name=='old' else ['--write-replay']))
for file in ['game.replay.checksums','game.replay','final.game.gz','checkpoint-32.game.gz']:
 assert (out/'game-old'/file).read_bytes()==(out/'game-new'/file).read_bytes(),file
for name,binary,prefix in [('old',base,['--run-game','--output-dir']),('new',new,['game','run','--output-dir'])]:
 run(binary,'continue-'+name,prefix+[out/('continue-'+name),'--load-game',out/'game-old/checkpoint-32.game.gz','--ticks','64','--compute-threads','1','--telemetry','checksums','--save','final']+(['--replay','true'] if name=='old' else ['--write-replay']))
assert (out/'continue-old/game.replay.checksums').read_bytes()==(out/'continue-new/game.replay.checksums').read_bytes()
for name,binary,args in [('old',base,['--verify-match']),('new',new,['match','verify'])]:
 run(binary,'verify-'+name,args+[root/'test/fixtures/multiplayer/FourSquares1.g2mr',('--map' if name=='old' else '--map-file'),root/'maps/FourSquares1.map.gz']+(['--out'] if name=='old' else ['--output-dir'])+[out/('verify-'+name),'--compute-threads','1'])
for file in ['checksums.txt','verdict.json']:
 assert (out/'verify-old'/file).read_bytes()==(out/'verify-new'/file).read_bytes(),file
(out/'equivalence.json').write_text(json.dumps({'catalog_equal':True,'map_bytes_equal':True,'per_tick_checksums_equal':True,'saves_equal':True,'replay_equal':True,'save_continuation_equal':True,'verdict_equal':True,'base':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()},indent=2))
print('Catalog, generated map, per-tick checksums, saves, replay, continuation and match verdict match.')
