import concurrent.futures,json,subprocess,time
from pathlib import Path
BASE=Path('/Users/bradley/glob2/output/maxima-defense-fix');CONFIG='/Users/bradley/glob2/output/maxima-live-controls-v21/ssh-config'
ROOT='/home/bradley/glob2-maxima-defense-fix';CAMPAIGN=ROOT+'/output/qualification'
def run(host,cpu):
 def ssh(cmd):return subprocess.run(['ssh','-F',CONFIG,host,cmd],check=True,capture_output=True,text=True).stdout
 if host!='therig.local':
  ssh('mkdir '+ROOT)
  subprocess.run(['scp','-F',CONFIG,str(BASE/'fleet.tar.gz'),host+':'+ROOT+'/fleet.tar.gz'],check=True)
  ssh('cd '+ROOT+' && tar -xzf fleet.tar.gz')
 with (BASE/(host+'-qualification.log')).open('w') as log:
  subprocess.run(['ssh','-F',CONFIG,host,f'cd {ROOT} && nice -n 10 taskset -c {cpu} python3 tools/qualify_maxima_no_orders.py {CAMPAIGN} --output {CAMPAIGN}/no-orders'],stdout=log,stderr=subprocess.STDOUT,check=True)
 code=f'''import json,sys
from pathlib import Path
sys.path.insert(0,'{ROOT}/tools');import maxima_win_experiment as e
out=Path('{CAMPAIGN}');p=json.loads((out/'protocol.json').read_text());e.verify_freeze(p)
v=json.loads((out/'no-orders/PASS.json').read_text());assert v['passed'] and len(v['cases'])==40
rows=[]
for f in sorted((out/'no-orders').glob('case-*/*/receipt.json')):
 r=json.loads(f.read_text());rows.append({{'case':str(f.relative_to(out/'no-orders')),'start':e.deterministic_signature(r['start']),'terminal':e.deterministic_signature(r['terminal'])}})
assert len(rows)==120
print(json.dumps({{'protocol_id':p['protocol_id'],'passed':True,'cases':40,'signature_sha256':e.identity(rows)}}))
'''
 result=subprocess.run(['ssh','-F',CONFIG,host,'python3 -'],input=code,text=True,capture_output=True,check=True)
 return host,json.loads(result.stdout)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 results=dict(pool.map(lambda v:run(*v),[('devlaptop.local',0),('pharaoh-dev-2.local',3),('pharaoh-dev-3.local',3),('therig.local',31)]))
assert len({v['signature_sha256'] for v in results.values()})==1
(BASE/'FLEET_PASS.json').write_text(json.dumps({'time':time.time(),'passed':True,'hosts':results},indent=2)+'\n')
print('All four hosts: 40 cases and identical signatures PASS',flush=True)
