import hashlib,json
from pathlib import Path
root=Path.cwd(); out=root/'artifacts/browser-fixes/merge-validation'
native=(out/'native-determinism/native.replay.checksums').read_bytes()
golden=(out/'native-determinism/verify-match/checksums.txt').read_bytes()
rows=[]
for path in sorted((out/'engine-results').rglob('*')):
 if not path.is_file() or path.name not in ['wasm.replay.checksums','verify-match.checksums.txt']:continue
 reference=native if path.name=='wasm.replay.checksums' else golden
 content=path.read_bytes(); rows.append({'file':path.relative_to(out).as_posix(),'bytes':len(content),'sha256':hashlib.sha256(content).hexdigest(),'matches_native':content==reference})
assert len(rows)==21,len(rows)
assert all(r['matches_native'] for r in rows),rows
(out/'trace-comparisons.json').write_text(json.dumps({'fixture':'games/cross-replay.game.gz','seed':42,'ticks':1500,'fixture_sha256':hashlib.sha256((root/'games/cross-replay.game.gz').read_bytes()).hexdigest(),'native_trace_sha256':hashlib.sha256(native).hexdigest(),'native_match_sha256':hashlib.sha256(golden).hexdigest(),'comparisons':rows},indent=2)+'\n')
print(len(rows),'browser traces byte-identical to native')
