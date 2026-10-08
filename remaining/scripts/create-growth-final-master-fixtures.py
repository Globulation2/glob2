from pathlib import Path
import subprocess,json,sys,gzip,hashlib,shutil
sys.path.insert(0,'test');from benchmark_parallel_compute import execute,digest
root=Path.cwd();b=root/'artifacts/resource-growth/remaining';cwd=b/'final-master-src';out=b/'final-master-compat';out.mkdir(exist_ok=True)
cmd=[sys.executable,'test/run_tests.py','--binary','engine','--no-display','--filter','BuildingArtwork/portable bundle*','--artifacts',str(out/'artwork'),'--junit',str(out/'artwork.xml')]
with (out/'artwork.log').open('w') as f:subprocess.run(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,check=True)
paths=list((out/'artwork').rglob('building145.game'));assert len(paths)==1,paths
fixtures=root/'test/fixtures/resources/growth-save-layout';(fixtures/'building145.game.gz').write_bytes(gzip.compress(paths[0].read_bytes(),mtime=0))
s=next(s for s in json.load(open(b/'manifest.json'))['scenarios'] if s['id']=='sparse');args=s['args'].copy();args[args.index('--ticks')+1]='1';args+=['--compute-threads','4','--save','final'];execute(cwd/'build/linux/client/release/src/glob2',args,out/'empty',cwd=cwd)
shutil.copy2(out/'empty/final.game.gz',fixtures/'empty-building145.game.gz')
p=fixtures/'manifest.json';m=json.load(p.open());m['purpose']='Preserve both independent format144/145 artwork/growth lineages and format146 pending growth through combined format147.'
revision=subprocess.check_output(['git','rev-parse','67fd5b935'],text=True).strip()
m['building145']={'source_revision':revision,'generator':'BuildingArtwork/portable bundle verifies image bytes and catalog frame references; export the binary MemoryStreamBackend after Game::save','fixture_writer':'final-master-fixture.patch in resource-growth PR evidence','binary_sha256':digest(cwd/'build/linux/client/release/test/glob2-engine-tests')}
m['empty-building145']={'source_revision':revision,'generator':'load frozen sparse128 fixture, advance one tick, --save final','binary_sha256':digest(cwd/'build/linux/client/release/src/glob2')}
m['growth146']={'source_revision':'9e9497d7151effae8fdd00a48db515511d30077a','generator':'frozen sparse128 fixture, advance1024ticks, delay8/shared4, --save final'}
m['sha256']={p.name:digest(p) for p in fixtures.glob('*.gz')};p.write_text(json.dumps(m,indent=2)+'\n')
(out/'commands.json').write_text(json.dumps({'artwork':cmd,'empty_args':args},indent=2));print('Created building-artwork save fixtures',flush=True)
