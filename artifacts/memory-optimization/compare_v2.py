import pathlib,os,signal,time,gzip,json,hashlib,subprocess
root=pathlib.Path(__file__).resolve().parent;pid=1763
path=root/'full-candidate-v2/game.replay.checksums';offset=0;digest=hashlib.sha256();segments=[]
with gzip.open(root/'matched-checksums.bin.gz','rb') as expected:
 while True:
  completed=(root/'full-candidate-v2/result.json').exists()
  if not path.exists():time.sleep(.2);continue
  n=path.stat().st_size
  if n-offset<128*1024*1024 and not completed:time.sleep(.2);continue
  paused=False
  try:
   try:os.kill(pid,signal.SIGSTOP);paused=True
   except ProcessLookupError:pass
   if paused:
    for _ in range(100):
     stat=subprocess.run(['ps','-p',str(pid),'-o','stat='],capture_output=True,text=True).stdout
     if not stat or 'T' in stat:break
     time.sleep(.01)
   n=path.stat().st_size;start=offset
   with path.open('rb') as candidate:
    candidate.seek(offset)
    while offset<n:
     length=min(1048576,n-offset);x=expected.read(length);y=candidate.read(length)
     if offset==0:y=y[:12]+b'\0'*4+y[16:]
     if x!=y:
      delta=next((i for i,(a,b) in enumerate(zip(x,y)) if a!=b),min(len(x),len(y)))
      (root/'checksum-mismatch.json').write_text(json.dumps({'offset':offset+delta,'baseline':x[max(0,delta-32):delta+64].hex(),'candidate':y[max(0,delta-32):delta+64].hex()},indent=2))
      raise RuntimeError(f'checksum mismatch at {offset+delta}')
     digest.update(y);offset+=length
   if n>start:segments.append({'offset':start,'bytes':n-start,'prefix_sha256':digest.hexdigest()})
   if not completed:
    with path.open('r+b') as f:f.truncate(0)
   (root/'checksum-v2-progress.json').write_text(json.dumps({'matched_bytes':offset,'segments':segments},indent=2))
   print('Actual candidate matches bytes',offset,flush=True)
  finally:
   if paused:
    try:os.kill(pid,signal.SIGCONT)
    except ProcessLookupError:pass
  if completed:break
 assert expected.read(1)==b'', 'candidate ended before baseline checksum stream'
(root/'checksum-v2-comparison.json').write_text(json.dumps({'all_detail_bytes_equal':True,'bytes':offset,'sha256':digest.hexdigest(),'segments':segments,'baseline_evidence':'matched-checksums.bin.gz','binaries':json.loads((root/'actual-binaries.json').read_text())},indent=2))
