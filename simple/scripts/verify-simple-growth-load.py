from pathlib import Path
import json,sys,os
sys.path.insert(0,'test')
from benchmark_parallel_compute import execute
root=Path.cwd();out=root/'artifacts/resource-growth/simple/continuation-final';exe=root/'build/linux/client/release/src/glob2';old=root/'artifacts/resource-growth/simple/v144-pending/final.game.gz';rows=[]
def run(name,save,ticks,mode,keep=False):
 args=['--load-game',str(save),'--ticks',str(ticks),'--compute-threads','4','--resource-growth-execution',mode,'--telemetry','checksums']
 if keep:args+=['--save','final']
 row=execute(exe,args,out/name);rows.append({'name':name,**row});return row
run('migrated',old,64,'shared',True)
a=run('continued',out/'migrated/final.game.gz',128,'shared');b=run('direct',old,128,'owner');# The saved format contributes to MapHeader::checkSum. The only input
# metadata difference is 144 -> 145: header rotr1 followed by Game's
# 4 + teamCount + playerCount rotations. For this two-team/two-player
# fixture, the exact XOR contribution is bit 23.
header_delta = (144 ^ 145)
rotations = 1 + 4 + 2 + 2
header_delta = ((header_delta >> rotations) | (header_delta << (32-rotations))) & 0xffffffff
assert a['result']['finalChecksum'] == (b['result']['finalChecksum'] ^ header_delta)
def trace(name):return dict(line.split() for line in (out/name/'world.checksums').read_text().splitlines())
x=trace('continued');y=trace('direct');assert all(int(y[k]) ^ header_delta == int(v) for k,v in x.items())
(out/'summary.json').write_text(json.dumps({'passed':True,'format_header_xor':header_delta,'runs':rows,'matching_continuation_ticks':len(x)},indent=2));print('v144 migration, v145 continuation and owner/shared checksum continuation passed')
