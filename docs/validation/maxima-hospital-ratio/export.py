"""Retain verbatim relevant engine records plus hashes of full remote artifacts."""
import gzip,hashlib,json,pathlib,re,shutil,sys,tarfile
root=pathlib.Path(sys.argv[1]);out=pathlib.Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
design=json.loads((root/'design.json').read_text());(out/'design.json').write_text(json.dumps(design,indent=2)+'\n')
for v in design['variants']:
 for case in design['cases']:
  source=root/v/case['id'];dest=out/v/case['id']
  if (dest/'source-hashes.json').exists() and json.loads((dest/'source-hashes.json').read_text()).get('projection_version')==2 and (dest/'execution.json').read_bytes()==(source/'execution.json').read_bytes():continue
  if not (source/'execution.json').exists() or json.loads((source/'execution.json').read_text())['exit']!=0:continue
  dest.mkdir(parents=True,exist_ok=True);team=case['players'].index('maxima')
  for name in ['execution.json','command.json','result.json']:
   shutil.copyfile(source/name,dest/name)
  hashes={"projection_version":2}
  for name in ['stdout.log.gz','initial.game.gz','final.game.gz']:
   h=hashlib.sha256()
   with (source/name).open('rb') as f:
    for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
   hashes[name]=dict(sha256=h.hexdigest(),bytes=(source/name).stat().st_size)
  with gzip.open(source/'stdout.log.gz','rt') as src,gzip.open(dest/'stdout.log.gz','wt',compresslevel=9) as dst:
   for line in src:
    if line.startswith('Maxima strategy:') or any(line.startswith(f'{prefix} team={team} ') for prefix in ['GLOB2_DEFENCE','GLOB2_DEFENCE_CONTACT','GLOB2_MEASURE','GLOB2_LABOUR']):dst.write(line)
  (dest/'source-hashes.json').write_text(json.dumps(hashes,indent=2)+'\n')
for case in design['cases']:
 dest=out/'maps'/case['id'];dest.mkdir(parents=True,exist_ok=True)
 for name in ['map-r0.map','command.json','result.json']:
  if (root/'maps'/case['id']/name).exists():shutil.copyfile(root/'maps'/case['id']/name,dest/name)
print('exported',sum(1 for v in design['variants'] for p in (out/v).glob('*/source-hashes.json')))
