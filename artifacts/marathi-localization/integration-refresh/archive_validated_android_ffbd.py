from pathlib import Path
import json,hashlib,gzip,shutil
root=Path.cwd();base=root/'artifacts/marathi-localization';evidence=base/'integration-refresh/android';archive=base/'validated-android-ffbd';archive.mkdir(exist_ok=True);cap=2*1024**3;free_floor=10*1024**3
sha=lambda path:hashlib.file_digest(Path(path).open('rb'),'sha256').hexdigest()
assert shutil.disk_usage(archive).free>=free_floor,'Archive requires10GiB free-space floor'
gate=json.loads((evidence/'gate-recommendation.json').read_text());producer=json.loads((evidence/'producer-provenance.json').read_text());assert gate['source_revision']==producer['final_revision']=='ffbd2aa743dad5f6b9b71bd8cc64f5c6d94d73c4';assert gate['result']=='pass';apk=Path(gate['unsigned_apk']['path']);signed=Path(gate['developer_signed_release_apk']['path']);elf=Path(gate['native_elf']['path']);assert sha(apk)==gate['unsigned_apk']['sha256'];assert sha(signed)==gate['developer_signed_release_apk']['sha256'];assert sha(elf)==gate['native_elf']['sha256'];symbols=apk.with_suffix('.symbols.json');assert json.loads(symbols.read_text())==gate['symbol_build_metadata']
def used():return sum(p.stat().st_size for p in archive.rglob('*') if p.is_file())
def copy(source,destination):
 destination.parent.mkdir(parents=True,exist_ok=True)
 if not destination.exists():
  assert used()+source.stat().st_size<cap,'Archive2GiB budget would be exceeded'
  shutil.copy2(source,destination)
 assert sha(source)==sha(destination)
 return {'original_path':str(source),'archive_path':str(destination),'sha256':sha(destination),'bytes':destination.stat().st_size,'encoding':'identity'}
rows=[copy(s,archive/s.name) for s in [apk,signed,symbols,signed.with_suffix('.json')]]
dest=archive/'libmain.so.gz'
if not dest.exists():
 initial=used()
 with elf.open('rb') as inp,dest.open('wb') as out:
  with gzip.GzipFile(filename='',mode='wb',fileobj=out,compresslevel=1,mtime=0) as compressed:
   while chunk:=inp.read(1024*1024):
    compressed.write(chunk);assert initial+out.tell()<cap,'Archive compression exceeded2GiBcap'
h=hashlib.sha256();decoded=0
with gzip.open(dest,'rb') as inp:
 while chunk:=inp.read(1024*1024):h.update(chunk);decoded+=len(chunk)
assert h.hexdigest()==gate['native_elf']['sha256'] and decoded==elf.stat().st_size
rows.append({'original_path':str(elf),'archive_path':str(dest),'original_sha256':h.hexdigest(),'original_bytes':decoded,'archive_sha256':sha(dest),'bytes':dest.stat().st_size,'encoding':'lossless gzip level1; mtime0'})
proof={}
for source in sorted(evidence.rglob('*')):
 if source.is_file() and '__pycache__' not in source.parts:
  rel=source.relative_to(evidence);row=copy(source,archive/'evidence'/rel);proof[str(rel)]={'sha256':row['sha256'],'bytes':row['bytes']}
manifest={'validated_revision':gate['source_revision'],'scope':'Complete verified currentffbd Android outputs and immutable snapshot of own Android evidence/provenance before subsequent integration rebuild. No claim this producer matches newmaster.','producer_history':{'initial':producer['initial_native_producer_revision'],'previous_verified':producer['previous_verified_revision'],'official_integrated_final':producer['final_revision']},'symbol_build_metadata':gate['symbol_build_metadata'],'byte_cap':cap,'archive_bytes_before_manifest':used(),'free_space_floor':free_floor,'outputs':rows,'evidence_snapshot_sha256':proof,'evidence_original_directory':str(evidence),'old_ba890_archive_preserved':str(base/'validated-android-ba890'),'preservation_notes':['Earlier ba890 archive is retained separately and unchanged.','Original working build paths may be overwritten by future official builds; archived files preserve actual ffbd bytes.','Current mutable evidence directory may later gain audit-only data; archived evidence/ is this exact completed Android snapshot.','Task AVD is preserved in place and ownedruntime is alreadycleaned.','Binary archive stays local; only manifest belongs in published review evidence.']}
encoded=json.dumps(manifest,indent=2)+'\n';assert used()+len(encoded.encode())<cap;(archive/'archive-manifest.json').write_text(encoded);assert used()<cap
print(json.dumps({'archive':str(archive),'bytes':used(),'cap':cap,'unsigned':gate['unsigned_apk']['sha256'],'signed':gate['developer_signed_release_apk']['sha256'],'raw_elf':h.hexdigest(),'build_id':gate['symbol_build_metadata']['build_id'],'evidence_files':len(proof),'manifest_sha256':sha(archive/'archive-manifest.json')},indent=2))
