from pathlib import Path
import gzip,hashlib,io,json,tarfile
E=Path(__file__).resolve().parent;B=E.parents[1];W=Path('/Users/bradley/.codex/worktrees/gradient-master-integration/glob2');objects={};entries=[]
def add(p,name):
 data=p.read_bytes();digest=hashlib.sha256(data).hexdigest();objects.setdefault(digest,data);entries.append(dict(path=str(name),sha256=digest,bytes=len(data),original_path=str(p)))
allowed={'.json','.log','.xml','.txt','.py','.checksums','.csv','.patch','.gz'}
for label in ['macos184','linux184']:
 base=B/label
 for p in sorted(base.rglob('*')):
  if not p.is_file() or p.suffix not in allowed or any(x in {'profile','frozen','prior183-binaries','__pycache__'} for x in p.relative_to(base).parts):continue
  if p.suffix=='.gz' and not p.name.endswith(('.game.gz','.map.gz')):continue
  add(p,Path(label)/p.relative_to(base))
add(B/'CROSS-PLATFORM-FINAL184.json','CROSS-PLATFORM-FINAL184.json')
old=B.parents[0]/'round-005/candidate-gen005/runtime-macos'
for name in ['input-job.json','map.map.gz']:add(old/name,Path('inputs')/name)
add(B.parents[0]/'round-004/candidate-gen004/runtime-macos/mature-t1-probeoff/final.game.gz','inputs/legacy179-warm.game.gz')
fixture=json.loads((B/'linux184/FINAL-FIXTURES-READY.json').read_text())
for n,sha in fixture['files'].items():
 p=W/n;assert hashlib.sha256(p.read_bytes()).hexdigest()==sha;add(p,Path('fixtures')/n)
index=dict(schema=1,scope='Final184 A24/min32/8publication/1024cap native qualification: ordinary Mac/Linux commands/logs,64 Linux focus+17goldens,8runtime/platform raw checksum/save/replay/diagnostic files and exact original inputs. No binaries/dependencies/compiler objects;183 history remains separately preserved.',logical_files=len(entries),unique_objects=len(objects),entries=entries)
(E/'CAS.json').write_text(json.dumps(index,indent=2)+'\n');archive=E/'native184-cas.tar.gz'
with archive.open('wb') as raw:
 with gzip.GzipFile(filename='',mode='wb',fileobj=raw,mtime=0) as gz:
  with tarfile.open(fileobj=gz,mode='w') as tar:
   for digest,data in sorted(objects.items()):
    info=tarfile.TarInfo('objects/'+digest);info.size=len(data);info.mode=0o644;info.mtime=0;tar.addfile(info,io.BytesIO(data))
with tarfile.open(archive) as tar:
 for digest,data in objects.items():assert tar.extractfile('objects/'+digest).read()==data
receipt=dict(status='PASS',archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),archive_bytes=archive.stat().st_size,index_sha256=hashlib.sha256((E/'CAS.json').read_bytes()).hexdigest(),logical_files=len(entries),unique_objects=len(objects),all_objects_decoded_and_verified=True)
(E/'CAS-VERIFIED.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt))
