import datetime,hashlib,json,pathlib,subprocess,time
B=pathlib.Path(__file__).resolve().parent;R=json.loads((B/'STUDY-REQUEST.json').read_text())['remote_root'];deadline=time.monotonic()+48*3600
while time.monotonic()<deadline:
 try:
  raw=subprocess.check_output(['ssh','devlaptop.local','cat '+R+'/STATUS.json'],text=True,timeout=15);s=json.loads(raw);tmp=B/'STATUS.tmp';tmp.write_text(raw);tmp.replace(B/'STATUS.json')
  if s['status']in ['complete','failed']:
   subprocess.run(['rsync','-a','--exclude=source/','--exclude=*.o','--exclude=glob2','devlaptop.local:'+R+'/',str(B)+'/'],check=True,timeout=180)
   verified=False
   if s['status']=='complete':
    seal=json.loads((B/'SEALED.json').read_text());assert all(hashlib.sha256((B/p).read_bytes()).hexdigest()==h for p,h in seal['files'].items());verified=True
   (B/'MIRROR-COMPLETE.json').write_text(json.dumps({'status':s['status'],'remote':R,'local_sealed_files_verified':verified,'updated_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()},indent=2)+'\n');break
 except Exception as e:
  (B/'mirror-last-error.json').write_text(json.dumps({'error':repr(e),'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()},indent=2))
 time.sleep(30)
