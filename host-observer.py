import psutil,time,json,sys
from pathlib import Path
out=Path('artifacts/serial-opt')
with (out/'native-host-load.jsonl').open('a',buffering=1) as f:
 while not (out/'native-driver.exit').exists():
  usage=psutil.cpu_percent(interval=1,percpu=True)
  active=[]
  for p in psutil.process_iter(['pid','name','cpu_times']):
   if p.info['name'] in ['cc1plus','cc1','wasm-opt','glob2','engine-tests','ld','lto1']:
    active.append({'pid':p.pid,'name':p.info['name'],'cpu_seconds':sum(p.info['cpu_times'][:2])})
  f.write(json.dumps({'unix_time':time.time(),'cpu_percent':usage,'load_average':psutil.getloadavg(),'processes':active})+'\n')
  time.sleep(4)
