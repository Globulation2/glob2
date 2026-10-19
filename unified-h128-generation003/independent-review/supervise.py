import pathlib,os,fcntl,time,json,subprocess,sys,traceback,datetime
D=pathlib.Path(__file__).resolve().parent
R=D.parent/'cumulative-512-research'
lock=(D/'review.lock').open('a');fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
def status(s,**kw):
 p=D/'STATUS.json';t=p.with_suffix('.tmp');t.write_text(json.dumps(dict(status=s,pid=os.getpid(),utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),**kw),indent=2)+'\n');t.replace(p)
if (D/'REVIEW.json').exists():status('EXISTING-REVIEW-NO-RERUN');sys.exit(0)
status('WAITING-EXISTING-FIT')
while True:
 p=R/'fit-status.json'
 if p.exists():
  v=json.loads(p.read_text());s=v.get('status','')
  if s=='COMPLETE':break
  if 'FAILED' in s or s=='COMPLETE-WITH-FIT-BLOCKER':status('UPSTREAM-FIT-BLOCKER',upstream=v);sys.exit(1)
 time.sleep(30)
os.nice(19);os.sched_setaffinity(0,{0})
for k in ['OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','MKL_NUM_THREADS','NUMEXPR_NUM_THREADS']:os.environ[k]='1'
status('INFERENCE-ONLY-REVIEW')
with (D/'review.log').open('w') as log:
 result=subprocess.run([sys.executable,str(D/'review.py'),str(R)],stdout=log,stderr=subprocess.STDOUT)
status('COMPLETE' if result.returncode==0 else 'REVIEW-FAILED',returncode=result.returncode,fit_performed=False)
