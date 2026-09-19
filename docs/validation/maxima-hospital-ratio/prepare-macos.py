import concurrent.futures,gzip,json,os,pathlib,shutil,subprocess
ROOT=pathlib.Path('/home/bradley/glob2-maxima-defense-study-20260919');OUT=ROOT/'artifacts/defense-study';batch=OUT/'hospital-ratio';design=json.loads((batch/'design.json').read_text());selected=design['cases'][-6:]
root=OUT/'hospital-macos-initials';root.mkdir(exist_ok=True);(root/'design.json').write_text(json.dumps(dict(cases=selected,variants=design['variants']),indent=2))
def make(job):
 v,c=job;d=root/v/c['id'];d.mkdir(parents=True,exist_ok=True)
 if (d/'initial.game.gz').exists():return
 cmd=[str(OUT/'binaries'/('towers-lazy' if v=='control' else 'hospital-ratio')),'--run-game','--map-file',str(batch/'maps'/c['id']/'map-r0.map'),'--game-seed',str(c['game_seed']),'--ticks','1','--save','initial','--output-dir',str(d)]
 for ai in c['players']:cmd+=['--player',ai]
 if v!='control':cmd+=['--ai-param',f"{c['players'].index('maxima')}:military.hospital_beds_per_warrior_percent={v[4:]}"]
 with (d/'setup.log').open('w') as f:subprocess.run(cmd,cwd=OUT/'hospital-control-root' if v=='control' else ROOT,stdout=f,stderr=subprocess.STDOUT,check=True)
 (d/'command.json').write_text(json.dumps(cmd,indent=2));p=d/'initial.game';(d/'initial.game.gz').write_bytes(gzip.compress(p.read_bytes(),mtime=0));p.unlink()
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:list(pool.map(make,[(v,c) for c in selected for v in design['variants']]))
