import json,sys,os,time,subprocess,shutil,runpy,concurrent.futures
from pathlib import Path
from unittest.mock import patch
BASE=Path(__file__).resolve().parent;sys.path.insert(0,str(BASE/'runtime'))
import maxima_win_experiment as e,maxima_win_report as report,maxima_experiment_fleet as fleet,maxima_win_statistics as stats
ROOT=Path('/home/bradley/glob2-maxima-conversion-fixed');REMOTE=ROOT/'output/barrier-large';CONFIG='/Users/bradley/glob2/output/maxima-live-controls-v21/ssh-config'
os.environ['MAXIMA_FLEET_SSH_CONFIG']=CONFIG;runpy.run_path(str(BASE/'runtime-policy.py'))['install']()
p=json.loads((BASE/'protocol.json').read_text());plan=json.loads((BASE/'PLAN.json').read_text());alloc=json.loads((BASE/'allocations.json').read_text());e.HOSTS={h:v['jobs'] for h,v in alloc.items()};key=plan['switch']
e.atomic(BASE/'controller-process.json',{'pid':os.getpid(),'started':time.time(),'fixed_pairs':21000,'slots':sum(e.HOSTS.values()),'remote_root':str(ROOT),'remote_output':str(REMOTE)})
def batch_receipts(path,protocol,manifest,results):
 assert {j['execution_id'] for j in manifest['jobs']}<=results.keys()
 pairs=report.assemble(manifest,results)[key]
 e.atomic(path.with_name('PAIRS.json'),{'pairs':pairs,'manifest_sha256':e.identity(manifest),'result_input_id':e.identity(results),'repeat_checks':'report.assemble verified','not_a_statistical_look':True})
 return {'status':'batch_collected_no_analysis'}
def archive_logs(h,out):
 code="""from pathlib import Path
import gzip,hashlib,shutil,json
b=Path(%r);n=0
for p in (b/'runs').glob('*/*'):
 if not (p.name.endswith('.engine.log') or p.name.endswith('.audit.jsonl')):continue
 target=p.with_name(p.name+'.gz');temp=target.with_name(target.name+'.pending')
 with p.open('rb') as src,gzip.open(temp,'wb',compresslevel=1) as dst:shutil.copyfileobj(src,dst)
 def digest(f):
  h=hashlib.sha256()
  while chunk:=f.read(1024*1024):h.update(chunk)
  return h.digest()
 with p.open('rb') as a,gzip.open(temp,'rb') as c:assert digest(a)==digest(c)
 temp.replace(target);p.unlink();n+=1
print(json.dumps({'losslessly_compressed_logs':n}))
"""%str(out)
 r=subprocess.run(['ssh','-F',CONFIG,h,'python3 -'],input=code,text=True,capture_output=True,timeout=300)
 return h,{'exit':r.returncode,'output':r.stdout[-1000:],'error':r.stderr[-1000:]}
try:
 allpairs=[]
 for number in range(plan['batches']):
  if (BASE/'STOP_DISPATCH.json').exists():raise RuntimeError('Campaign stop requested')
  b=BASE/f'batch-{number:03d}';out=REMOTE/f'batch-{number:03d}'
  e.atomic(BASE/'STATUS.json',{'time':time.time(),'stage':'confirmation','active_batch':number,'completed_batches':number,'completed_pairs':len(allpairs),'scheduled_pairs':21000,'batch_status_path':str(b/'STATUS.json'),'no_interim_inference':True})
  if not (b/'manifest.json').exists():
   b.mkdir(exist_ok=True)
   for n in ['protocol.json','qualification.json','allocations.json','allocation.json']:shutil.copy2(BASE/n,b/n)
   if not (b/'evidence').exists():shutil.copytree(BASE/'evidence',b/'evidence')
   original=e.scenario
   with patch.object(e,'scenario',side_effect=lambda protocol,stage,index,balanced=False:original(protocol,stage,2000+number*500+index,balanced)):
    m=report.comparisons(p,'confirmation',{key:500},[key])
   a=fleet.assign(m,alloc)
   for v in a.values():v['weak']+=v['strong'];v['strong']=[]
   e.atomic(b/'assignments.json',a);e.atomic(b/'manifest.json',m)
  e.require_gates(b,p,'confirmation')
  if not (b/'PAIRS.json').exists():
   files=[str(f.relative_to(b)) for f in b.rglob('*.json') if 'results' not in f.parts and f.name not in ('STATUS.json','supervisor.json')]
   for h in e.HOSTS:
    subprocess.run(['ssh','-F',CONFIG,h,'mkdir -p '+str(out)],check=True,timeout=20)
    subprocess.run(['rsync','-az','-e','ssh -o ConnectTimeout=8 -F '+CONFIG,'--files-from=-',str(b)+'/',h+':'+str(out)+'/'],input='\n'.join(files)+'\n',text=True,check=True,timeout=60)
    fleet.remote(h,ROOT,out,'start','weak')
   with patch.object(report,'publish_final',side_effect=batch_receipts):fleet.supervise(b,ROOT,out)
   if not (b/'PAIRS.json').exists():raise RuntimeError('Batch requires investigation; preserve all jobs and resume this batch after resolution, not restart campaign')
   for h in e.HOSTS:fleet.remote(h,ROOT,out,'stop',value={'reason':'Operational batch collected; next registered batch follows','active_engines':'none'})
   with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:archived=dict(pool.map(lambda h:archive_logs(h,out),e.HOSTS))
   e.atomic(b/'ARCHIVE.json',archived)
  allpairs+=json.loads((b/'PAIRS.json').read_text())['pairs']
 assert len(allpairs)==21000 and len({r['scenario_id'] for r in allpairs})==21000
 e.atomic(BASE/'FINAL_RESULT.json',{'protocol_id':p['protocol_id'],'switch':key,'fixed_pairs':21000,'alpha':plan['alpha'],'result':stats.analyze(allpairs,plan['alpha']),'pairs_sha256':e.identity(allpairs),'old_outcomes_pooled':False,'default_change_permitted':False})
 e.atomic(BASE/'STATUS.json',{'time':time.time(),'stage':'complete','completed_pairs':21000,'scheduled_pairs':21000,'report':'FINAL_RESULT.json'})
except Exception as error:
 e.atomic(BASE/'CONTROLLER_ERROR.json',{'time':time.time(),'error':str(error),'policy':'preserve results and queues; diagnose/resume, never discard full run or retry uncertain jobs implicitly'});raise
