"""Restore deduplicated evidence paths from a verified checkpoint archive."""
import argparse,hashlib,json,pathlib,shutil,zipfile
p=argparse.ArgumentParser(__doc__);p.add_argument('archive',type=pathlib.Path);p.add_argument('destination',type=pathlib.Path);a=p.parse_args();root=a.destination.resolve();root.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(a.archive) as z:
 inventory=json.loads(z.read('file-inventory.json'))
 for name,rec in inventory.items():
  target=(root/name).resolve()
  if not target.is_relative_to(root):raise ValueError('Unsafe output path: '+name)
  if target.exists():raise FileExistsError(target)
  target.parent.mkdir(parents=True,exist_ok=True);h=hashlib.sha256();size=0
  with z.open(rec['stored_as']) as source,target.open('wb') as sink:
   for block in iter(lambda:source.read(1024*1024),b''):sink.write(block);h.update(block);size+=len(block)
  if size!=rec['bytes'] or h.hexdigest()!=rec['sha256']:raise ValueError('Evidence integrity mismatch: '+name)
print('Restored',len(inventory),'verified evidence paths')
