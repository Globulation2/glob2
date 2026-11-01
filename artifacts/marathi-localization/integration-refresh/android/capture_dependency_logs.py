from pathlib import Path
import gzip,json,time,hashlib
base=Path('artifacts/marathi-localization/integration-refresh/android');work=Path('/tmp/glob2-dependency-p7ogeqr7');limit=8*1024*1024;max_file=4*1024*1024;record={'source':str(work),'scope':'Only task-owned recording temporary staging','compressed_byte_cap':limit,'source_file_byte_cap':max_file,'files':{},'omitted':{}}
while work.exists():
 used=0
 for path in sorted(work.rglob('*.log')):
  name=str(path.relative_to(work));size=path.stat().st_size
  if size>max_file:record['omitted'][name]={'bytes':size,'reason':'source-file cap, no truncation'};continue
  data=path.read_bytes();packed=gzip.compress(data,compresslevel=1,mtime=0)
  if used+len(packed)>limit:record['omitted'][name]={'bytes':size,'reason':'compressed-total cap, no truncation'};continue
  used+=len(packed);dst=base/'dependency-logs'/Path(name+'.gz');dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(packed);record['files'][name]={'bytes':size,'sha256':hashlib.sha256(data).hexdigest(),'compressed_bytes':len(packed),'snapshot_complete_file':True}
 record['last_snapshot_compressed_bytes']=used;(base/'dependency-log-capture.json').write_text(json.dumps(record,indent=2)+'\n');time.sleep(10)
record['temporary_staging_removed']=True;(base/'dependency-log-capture.json').write_text(json.dumps(record,indent=2)+'\n')
