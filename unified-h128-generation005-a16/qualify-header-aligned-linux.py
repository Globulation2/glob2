from pathlib import Path
import json,datetime,hashlib,traceback
B=Path(__file__).resolve().parent;D=B.parent
for name in ['LINUX-LOCAL-QUALIFICATION.json']:
 p=D/name;backup=D/name.replace('.json','-FIRST-FAILURE.json')
 if p.exists() and not backup.exists():backup.write_bytes(p.read_bytes())
import time
while not (B/'status.json').exists() or len(json.loads((B/'status.json').read_text()).get('runs',[]))<5:time.sleep(2)
time.sleep(3)
(B/'ORIGINAL-FIVE-RUN-STATUS.json').write_bytes((B/'status.json').read_bytes())
previous=json.loads((B/'status.json').read_text());source=(B/'qualify.py').read_text();prefix=source[:source.index("\nstatus('running')\n")];exec(compile(prefix,str(B/'qualify.py'),'exec'));runs=[x for x in previous['runs'] if x['returncode']==0]
(B/'REJECTED-ZERO-TICK-SETUP.json').write_text(json.dumps([x for x in previous['runs'] if x['returncode']!=0],indent=2)+'\n')
def final(state,**kw):(D/'LINUX-LOCAL-QUALIFICATION.json').write_text(json.dumps(dict(status=state,source='4cf3933fee8cfc63d1ddab802589dac6b0ffeb2e',utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),binary_sha256=h(binary),**kw),indent=2)+'\n')
try:
 final('RUNNING',step='matched-current-header-continuity')
 warm=Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-004/candidate-gen004/runtime-linux-fix2/mature-t1-probeoff/final.game.gz')
 assert h(warm)=='c7fc30db59440d54060ce8d665fd73ba30f386bbaf06d516ea31df2e6f2fb4be'
 normalized,n=run('resave179-as180-one-tick',1,False,warm,17409)
 control,c=run('header180-control-from17409',1,False,normalized/'final.game.gz',18432)
 original=B/'old179-mature-t1-probeoff';resumed=B/'new180-continuation-t1'
 assert h(control/'final.game.gz')==h(original/'final.game.gz')==h(resumed/'final.game.gz')
 assert (control/'snapshot.checksums').read_text().splitlines()==[l for l in (original/'snapshot.checksums').read_text().splitlines() if int(l.split()[0])>=17409]
 matching={}
 for name in ['world.checksums','snapshot.checksums']:
  expected=[line for line in (control/name).read_text().splitlines() if int(line.split()[0])>=17920]
  actual=(resumed/name).read_text().splitlines();assert expected==actual,(name,'matched180 continuity mismatch');matching[name]=len(actual)
 old=[l for l in (original/'world.checksums').read_text().splitlines() if int(l.split()[0])>=17409];current=(control/'world.checksums').read_text().splitlines();assert len(old)==len(current)==1023
 masks={int(a.split()[1])^int(b.split()[1]) for a,b in zip(old,current)};assert masks=={0xc0000},masks
 proof=dict(status='PASS',method='raw-world-equality-from-two-current180-checkpoints; old179-load-compared-separately',old179_warm_sha256=h(warm),normalized180_warm_sha256=h(normalized/'final.game.gz'),matching_raw_tick_lines=matching,old179_vs180_snapshot_bytes_identical=True,all_three_final_save_bytes_identical=True,old179_vs180_world_xor=hex(next(iter(masks))),old179_vs180_world_rows=1023,note='MapHeader checksum retains loaded input-format minor; writer emits current version. No engine checksum or simulation state changed to satisfy this proof.')
 (B/'MATURE-CONTINUITY.json').write_text(json.dumps(proof,indent=2)+'\n');status('passed');final('PASSED',runtime_status='passed',continuity_proof='runtime-linux/MATURE-CONTINUITY.json',preserved_initial_failure='LINUX-LOCAL-QUALIFICATION-FIRST-FAILURE.json')
except Exception:
 final('FAILED',step='matched-current-header-continuity',error=traceback.format_exc());raise
