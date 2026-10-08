from pathlib import Path
import concurrent.futures,os,subprocess,json,time,hashlib
root=Path.cwd();out=root/'artifacts/resource-growth/remaining/generated-delay';f=json.loads((out/'freeze.json').read_text());f['sharded_branch_binary_sha256']=hashlib.sha256((root/'build/linux/client/release/test/glob2-engine-tests').read_bytes()).hexdigest();f['sharded_branch_harness_sha256']=hashlib.sha256((root/'src/map/ResourceGrowthBenchmark.cpp').read_bytes()).hexdigest();(out/'freeze.json').write_text(json.dumps(f,indent=2)+'\n')
def run(first,last):
 d=out/'branch-shards'/f'continents-{first}-{last}';d.mkdir(parents=True,exist_ok=True)
 env=dict(os.environ,LD_LIBRARY_PATH='/tmp/glob2-sdl3/prefix/lib',GLOB2_GROWTH_GENERATED_CASE='continents',GLOB2_GROWTH_GENERATED_TICKS='4096',GLOB2_GROWTH_GENERATED_WIDE='1',GLOB2_GROWTH_GENERATED_INPUT=str(out/'master'),GLOB2_GROWTH_GENERATED_OUTPUT=str(d/'results.json'),GLOB2_GROWTH_GENERATED_DELAYS='1,2,3,4,8,12,16',GLOB2_GROWTH_GENERATED_SEED_BEGIN=str(first),GLOB2_GROWTH_GENERATED_SEED_END=str(last))
 command=['python3','test/run_tests.py','--binary','engine','--no-display','-j1','--timeout','3600','--filter','ResourceGrowthBenchmark/generated*','--junit',str(d/'junit.xml'),'--artifacts',str(d/'test')]
 (d/'command.json').write_text(json.dumps({'cwd':str(root),'environment':{k:v for k,v in env.items() if k.startswith('GLOB2_') or k=='LD_LIBRARY_PATH'},'command':command},indent=2)+'\n')
 start=time.time()
 with (d/'run.log').open('w') as log:result=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT)
 print(first,last,result.returncode,round(time.time()-start,1),flush=True);assert result.returncode==0
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:list(pool.map(lambda p:run(*p),[(2,6),(7,11),(12,16),(17,20)]))
partial=json.loads((out/'continents-interrupted/results.json').read_text());merged=dict(partial,samples=[x for x in partial['samples'] if x['seed']==1]);overlap=[]
for d in sorted((out/'branch-shards').iterdir()):
 data=json.loads((d/'results.json').read_text());assert not data['generation_failures']
 for row in data['samples']:
  for old in partial['samples']:
   if (old['seed'],old['delay'],old['variant'])==(row['seed'],row['delay'],row['variant']):
    assert {k:v for k,v in old.items() if k!='initial_checksum'}=={k:v for k,v in row.items() if k!='initial_checksum'}
    overlap.append({'seed':row['seed'],'delay':row['delay'],'variant':row['variant'],'original_initial_checksum':old['initial_checksum'],'shard_initial_checksum':row['initial_checksum']})
 merged['samples'].extend(data['samples'])
merged['samples'].sort(key=lambda r:(r['seed'],r['delay'],r['variant']))
assert len(merged['samples'])==280
(out/'branch/continents/results.json').write_text(json.dumps(merged,indent=2)+'\n');(out/'continents-shard-identity.json').write_text(json.dumps({'matched_overlap_rows':overlap,'total_rows':280,'seeds':list(range(1,21)),'reason':'New harness only adds seed-range selection; All duplicated ecology outputs, checkpoints, statistics and pipeline counts match exactly. Initial whole-game checksums differ across separately imported processes; no cross-process full-state identity is claimed. Within each run owner/shared per-tick checksums pass.'},indent=2)+'\n')
print('Merged all20 continent seeds; overlap matches.',flush=True)
