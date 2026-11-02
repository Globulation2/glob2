from pathlib import Path
import hashlib,json,subprocess,time
out=Path('artifacts/terrain-decoration-pr/linux'); p=out/'restore-render-build.json'
for _ in range(900):
 if p.exists(): break
 time.sleep(2)
else: raise SystemExit('restore wait timeout')
if json.loads(p.read_text())['exit']: raise SystemExit('final build failed')
rows={}
for rel in ['src/glob2','test/glob2-engine-tests','test/glob2-unit-tests']:
 a=Path('build/linux/client/release')/rel;b=out/'frozen-0be5'/rel
 rows[rel]={'sha256':hashlib.sha256(a.read_bytes()).hexdigest(),'byte_identical_to_tested_0be5':a.read_bytes()==b.read_bytes()}
report={'source':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'status':subprocess.check_output(['git','status','--porcelain'],text=True),'whitespace_ignored_diff':subprocess.check_output(['git','diff','--ignore-all-space','0be5f6a9f2','659ae9c956'],text=True),'binaries':rows,'all_identical':all(r['byte_identical_to_tested_0be5'] for r in rows.values())}
(out/'final659-qualification.json').write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
