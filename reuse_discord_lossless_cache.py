from pathlib import Path
import shutil,os,tempfile
source=Path('/Users/bradley/.codex/worktrees/2a0e/glob2/build/asset-cache')
cache=Path('build/asset-cache').resolve()
cache.mkdir(exist_ok=True)
copied=0
for metadata in source.glob('*/selection.json'):
 entry=metadata.parent
 if len(entry.name)!=64 or any(c not in '0123456789abcdef' for c in entry.name): continue
 target=cache/entry.name
 if target.exists(): continue
 temporary=Path(tempfile.mkdtemp(prefix='.reuse-discord-',dir=cache))
 try:
  for p in entry.iterdir():
   if p.is_file(): shutil.copy2(p,temporary/p.name)
  try:
   os.rename(temporary,target)
   copied+=1
  except OSError:
   if not target.exists(): raise
 finally:
  if temporary.exists(): shutil.rmtree(temporary)
print(f'Copied {copied} completed content-addressed asset entries; existing entries untouched.')
