import sys,os,json,runpy,time
from pathlib import Path
sys.path.insert(0,'/Users/bradley/glob2-maxima-mainline-update/tools')
import maxima_win_experiment as e,maxima_experiment_fleet as fleet
BASE=Path(__file__).resolve().parent;ROOT=Path('/home/bradley/glob2-maxima-mainline-100k-final');REMOTE=ROOT/'output/fresh-controls-100k'
os.environ['MAXIMA_FLEET_SSH_CONFIG']='/Users/bradley/glob2/output/maxima-live-controls-v21/ssh-config'
runpy.run_path('/Users/bradley/glob2/output/maxima-live-controls-v21/runtime-policy.py')['install']()
e.HOSTS={h:a['jobs'] for h,a in json.loads((BASE/'allocations.json').read_text()).items()}
try:
 if (BASE/'STOP_DISPATCH.json').exists():raise RuntimeError('Stopped; inspect before resuming')
 e.atomic(BASE/'controller-process.json',{'pid':os.getpid(),'started':time.time(),'remote_root':str(ROOT),'remote_output':str(REMOTE),'slots':sum(e.HOSTS.values()),'automatic_next_stage':False})
 for h in e.HOSTS:
  v=fleet.remote(h,ROOT,REMOTE,'pulse')
  if v['stop']:raise RuntimeError(str(v['stop']))
  fleet.remote(h,ROOT,REMOTE,'start','weak')
 fleet.supervise(BASE,ROOT,REMOTE)
 if json.loads((BASE/'STATUS.json').read_text()).get('status')=='stage_complete':
  for h in e.HOSTS:fleet.remote(h,ROOT,REMOTE,'stop',value={'reason':'Fresh controls complete; awaiting next-stage plan','active_engines':'none; all results collected'})
except Exception as error:
 e.atomic(BASE/'CONTROLLER_ERROR.json',{'time':time.time(),'error':str(error),'active_engines':'preserve; authority expires without pulses'});raise
