from pathlib import Path
import concurrent.futures,gzip,hashlib,json,os,subprocess,time
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'artifacts/terrain-vote/mean/integration-golden-checks'
BIN=ROOT/'build/linux/client/debug/dev-dev_fast-true-linker-auto/src/MapGeneratorGoldenTest'
TABLE=ROOT/'test/map-generator-golden.txt'
source=lambda: json.loads(subprocess.check_output(['python3','test/build_provenance.py'],cwd=ROOT))
identity=source(); assert not identity['dirty']
table_bytes=TABLE.read_bytes(); binary_hash=hashlib.sha256(BIN.read_bytes()).hexdigest()
rows=[s for s in table_bytes.decode().splitlines() if s.startswith('linux-x86_64 ')]
keys={tuple(s.split()[:7]) for s in rows}
inventory={tuple(s.split()) for s in (ROOT/'artifacts/terrain-vote/mean/inventory.txt').read_text().splitlines() if s.startswith('linux-x86_64 ')}
assert len(rows)==len(keys)==564 and keys==inventory
# A single-row table allows the existing checker to price each request in a
# fresh process. Union coverage above enforces the complete native inventory;
# absent unrelated recipes inside each isolated table are expected warnings.
rows.sort(key=lambda s:(1<<int(s.split()[3]))*(1<<int(s.split()[4]))*int(s.split()[5]),reverse=True)
OUT.mkdir(parents=True,exist_ok=False)
def run(item):
 index,row=item; work=OUT/f'{index:03d}';(work/'test').mkdir(parents=True)
 (work/'test/map-generator-golden.txt').write_text(row+'\n')
 started=time.monotonic()
 result=subprocess.run([str(BIN),'profile'],cwd=work,env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy'),capture_output=True,timeout=1800)
 output=result.stdout+result.stderr
 with gzip.open(work/'check.log.gz','wb') as log: log.write(output)
 record=dict(row=row,exit_code=result.returncode,seconds=time.monotonic()-started,checked_one=b'linux-x86_64: 1 golden rows compared, 0 failures' in result.stdout)
 (work/'result.json').write_text(json.dumps(record)+'\n');return record
records=[]
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
 for result in pool.map(run,enumerate(rows)):
  records.append(result)
  if len(records)%50==0: print(f'{len(records)}/564 fresh request checks collected',flush=True)
assert source()==identity and TABLE.read_bytes()==table_bytes
assert hashlib.sha256(BIN.read_bytes()).hexdigest()==binary_hash
summary=dict(source=identity,binary_sha256=binary_hash,table_sha256=hashlib.sha256(table_bytes).hexdigest(),rows=len(rows),records=records)
(OUT/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
failed=[r for r in records if r['exit_code'] or not r['checked_one']]
assert not failed,failed
print('PASS all564 final recipe keys, statuses and fingerprints match fresh isolated native processes',flush=True)
