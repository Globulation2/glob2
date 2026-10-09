from pathlib import Path
import subprocess,json,hashlib,re
root=Path.cwd();out=root/'artifacts/map-wrapping'
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
metadata=json.loads((out/'metadata.json').read_text());metadata['candidate']=(out/'candidate-revision.txt').read_text().strip();metadata['source_patch_sha256']=sha(out/'source.patch');metadata['build_commands']={name:json.loads((out/f'{name}-build-command.json').read_text()) for name in ('baseline','candidate')};metadata['binaries']={}
for name in ('baseline','candidate'):
 binary=out/f'{name}-glob2';metadata['binaries'][name]=sha(binary)
 with (out/f'{name}-elf-sections.txt').open('w') as f:subprocess.run(['readelf','-SW',str(binary)],stdout=f,check=True)
 assert '.debug_info' in (out/f'{name}-elf-sections.txt').read_text()
 with (out/f'{name}-cabino.asm').open('w') as f:subprocess.run(['objdump','-d','--disassemble=_ZNK6Cabino8Gradient9getHeightEii',str(binary)],stdout=f,check=True)
metadata['harnesses']={name:sha(out/name) for name in ('glob2-unit-tests','glob2-engine-tests')};(out/'metadata.json').write_text(json.dumps(metadata,indent=2))
summary={}
for name in ('baseline','candidate'):
 asm=(out/f'{name}-cabino.asm').read_text();ops=re.findall(r'^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2}\s+)+\s*([a-z0-9]+)',asm,re.M)
 summary[name]={'instructions':len(ops),'idiv':ops.count('idiv'),'and':ops.count('and')}
assert summary['baseline']['idiv']==2 and summary['candidate']['idiv']==0,summary
(out/'disassembly-summary.json').write_text(json.dumps(summary,indent=2));print(summary)
