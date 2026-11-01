from pathlib import Path
import json,hashlib,gzip,shutil,os
root=Path.cwd();base=root/'artifacts/marathi-localization';archive=base/'validated-android-ba890';archive.mkdir(exist_ok=True);budget=2*1024**3;free_floor=10*1024**3
assert shutil.disk_usage(archive).free>=free_floor
old=json.loads((base/'android-rendering/apk-and-install.json').read_text());assert old['final_official_producer_revision']=='ba890db53e67617fb3cb1033c157ca578781196e';producer=json.loads((base/'android-final-producer-provenance.json').read_text());apk=Path(producer['apk']['path']);signed=apk.with_name('app-release-development.apk');elf=root/'build/android/device/x86_64/24/client/release/lib/libmain.so'
def sha(path):
 h=hashlib.sha256()
 with Path(path).open('rb') as f:
  while chunk:=f.read(1024*1024):h.update(chunk)
 return h.hexdigest()
assert sha(apk)==old['unsigned_apk_sha256'] and sha(signed)==old['signed_apk_sha256'];rows=[]
for source in [apk,signed,apk.with_suffix('.symbols.json'),signed.with_suffix('.json')]:
 assert source.is_file();dest=archive/source.name
 if not dest.exists():shutil.copy2(source,dest)
 assert sha(source)==sha(dest);rows.append({'original_path':str(source),'archive_path':str(dest),'sha256':sha(dest),'bytes':dest.stat().st_size,'encoding':'identity'})
elfhash=sha(elf);dest=archive/'libmain.so.gz'
if not dest.exists():
 with elf.open('rb') as source,dest.open('wb') as output:
  with gzip.GzipFile(filename='',mode='wb',fileobj=output,compresslevel=1,mtime=0) as compressed:
   while chunk:=source.read(1024*1024):compressed.write(chunk)
h=hashlib.sha256();decoded=0
with gzip.open(dest,'rb') as f:
 while chunk:=f.read(1024*1024):h.update(chunk);decoded+=len(chunk)
assert h.hexdigest()==elfhash and decoded==elf.stat().st_size;rows.append({'original_path':str(elf),'archive_path':str(dest),'original_sha256':elfhash,'original_bytes':decoded,'archive_sha256':sha(dest),'bytes':dest.stat().st_size,'encoding':'gzip lossless; mtime0'})
proof_names=['android-final-producer-provenance.json','android-initial-producer-provenance.json','android-final-head-approval.json','android-object-inputs-before-final.json','android-object-inputs-after-final.json','android-evidence-gate-recommendation.json','android-runtime-cleanup.json','android-final-visual-review.json','android-overlay-visual-review.json','android-smoke-adaptation.json']
proof={}
for name in proof_names:
 p=base/name;target=archive/name
 if not target.exists():shutil.copy2(p,target)
 assert sha(p)==sha(target);proof[name]=sha(target)
for source,name in [(base/'android-rendering/apk-and-install.json','apk-and-install.json'),(base/'android-rendering/lifecycle/result.json','lifecycle-result.json')]:
 target=archive/name
 if not target.exists():shutil.copy2(source,target)
 assert sha(source)==sha(target);proof[name]=sha(target)
used=sum(p.stat().st_size for p in archive.rglob('*') if p.is_file());assert used<budget;record={'validated_revision':old['final_official_producer_revision'],'producer_history':{'initial':producer['initial_native_producer_revision'],'intermediate':producer['intermediate_resource_revision'],'official_final':producer['final_revision']},'byte_cap':budget,'archive_bytes':used,'free_space_floor':free_floor,'outputs':rows,'proof_file_sha256':proof,'evidence_directories_retained_in_place':['android-rendering','android-attempt1-system-ui-anr'],'preservation_notes':['Existing old-scope evidence remains intact and is not relabeled as the future integrated producer.','Original working build paths may be reused by official incremental builds; archived files preserve validated old bytes.','AVD files remain in place; future task-owned runtime tests may reuse that AVD, without deleting its files.']};(archive/'archive-manifest.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps({'archive':str(archive),'bytes':used,'cap':budget,'unsigned_apk_sha256':old['unsigned_apk_sha256'],'signed_apk_sha256':old['signed_apk_sha256'],'elf_original_sha256':elfhash},indent=2))
