import pathlib,os,signal,time,gzip,json,hashlib,subprocess
root=pathlib.Path(__file__).resolve().parent
pids=[96397,96408]
paths=[root/f'full-{tag}/game.replay.checksums' for tag in ['baseline','candidate']]
offset=0;digest=hashlib.sha256();segments=[]
with gzip.open(root/'matched-checksums.bin.gz','wb',compresslevel=1) as saved:
 while True:
  completed=all((root/f'full-{tag}/result.json').exists() for tag in ['baseline','candidate'])
  n=min(p.stat().st_size for p in paths)
  if n-offset<256*1024*1024 and not completed:time.sleep(1);continue
  paused=[]
  try:
   for pid in pids:
    try:os.kill(pid,signal.SIGSTOP);paused.append(pid)
    except ProcessLookupError:pass
   # SIGSTOP is process-wide; wait for all its threads to stop writing.
   for pid in paused:
    for _ in range(100):
     stat=subprocess.run(['ps','-p',str(pid),'-o','stat='],capture_output=True,text=True).stdout
     if not stat or 'T' in stat:break
     time.sleep(.01)
   n=min(p.stat().st_size for p in paths);start=offset
   with paths[0].open('rb') as a,paths[1].open('rb') as b:
    a.seek(offset);b.seek(offset)
    while offset<n:
     length=min(1048576,n-offset);x=a.read(length);y=b.read(length)
     if offset==0:
      x=x[:12]+b'\0'*4+x[16:];y=y[:12]+b'\0'*4+y[16:]
     if x!=y:raise RuntimeError(f'checksum mismatch at {offset}')
     saved.write(x);digest.update(x);offset+=length
   if n>start:segments.append({'offset':start,'bytes':n-start,'prefix_sha256':digest.hexdigest()})
   if not completed:
    for path in paths:
     with path.open('r+b') as f:
      f.seek(offset);tail=f.read();f.truncate(0)
      if tail:os.pwrite(f.fileno(),tail,offset)
   saved.flush()
   (root/'checksum-stream-progress.json').write_text(json.dumps({'matched_bytes':offset,'segments':segments},indent=2))
   print('Matched/compressed bytes',offset,flush=True)
  finally:
   for pid in paused:
    try:os.kill(pid,signal.SIGCONT)
    except ProcessLookupError:pass
  if completed:break
headers=[]
for p in paths:
 with p.open('rb') as f:headers.append(f.read(20).hex())
assert headers[0]==headers[1],headers
(root/'checksum-comparison.json').write_text(json.dumps({'all_detail_bytes_equal':True,'bytes':offset,'sha256_with_zero_tick_count_header':digest.hexdigest(),'final_headers':headers,'segments':segments,'compressed_evidence':'matched-checksums.bin.gz','note':'Streaming comparison reclaimed consumed raw sidecar blocks. Compressed header tick count is zero; final count is in final_headers.'},indent=2))
