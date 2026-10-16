from pathlib import Path
import json,hashlib,shutil,subprocess
O=Path(__file__).resolve().parent;E=O.parent/'october-regression/coordination-redesign/full-design-comparison/playground-stop/publish-checkout';D=E/'playground-pr1194-403dfd099';H=O.parent/'october-regression/coordination-redesign/full-design-comparison/campaign/serial/Playground'
def sha(p):
 with Path(p).open('rb')as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert json.loads((O/'complete.json').read_text())['state']=='complete' and json.loads((O/'results/receipt.json').read_text())['state']=='report_ready'
assert not D.exists();D.mkdir();entries=[]
def copy(p,rel):
 q=D/rel;q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q);entries.append({'path':str(rel),'bytes':q.stat().st_size,'sha256':sha(q),'original':str(p)})
for name in ['request.json','build.py','build-complete.json','build.log','run.py','launch.json','qualification.json','complete.json','analyze.py','publish.py','compare-previous.py','pipeline.py']:copy(O/name,Path(name))
for p in(O/'results').iterdir():
 if p.is_file():copy(p,Path('results')/p.name)
for p in(O/'qualification').rglob('*'):
 if p.is_file() and 'profile'not in p.parts and p.suffix in ['.json','.log','.txt']:copy(p,Path('qualification')/p.relative_to(O/'qualification'))
for p in(O/'runs').glob('*/receipt.json'):
 copy(p,Path('latest')/p.parent.name/'receipt.json');copy(p.with_name('stdout.log'),Path('latest')/p.parent.name/'stdout.log')
request=json.loads((O/'request.json').read_text())
for role in ['baseline','wake-only']:
 for seed in request['seeds']:
  p=H/(str(seed)+'-'+role)/'receipt.json';copy(p,Path('historical')/role/str(seed)/'receipt.json');copy(p.with_name('stdout.log'),Path('historical')/role/str(seed)/'stdout.log')
assert sha(request['map_file'])==request['map_sha256']
copy(Path(request['map_file']),Path('fixtures/Playground.map'))
R=Path(json.loads((O/'build-complete.json').read_text())['cwd'])
for name in ['test/fixtures/multiplayer/FourSquares1.g2mr','test/fixtures/multiplayer/FourSquares1.verify-trace.txt','maps/FourSquares1.map.gz']:copy(R/name,Path('fixtures')/Path(name).name)
(D/'verify.py').write_text('''from pathlib import Path
import hashlib,json,math,csv,re,statistics
R=Path(__file__).resolve().parent;m=json.loads((R/'manifest.json').read_text())
for x in m['files']:
 p=R/x['path']
 with p.open('rb')as f:d=hashlib.file_digest(f,'sha256').hexdigest()
 assert d==x['sha256'] and p.stat().st_size==x['bytes'],x['path']
plan=json.loads((R/'request.json').read_text());groups={}
for role in ['baseline','wake-only','latest-master']:
 rows=[]
 for seed in plan['seeds']:
  p=R/'latest'/str(seed)/'receipt.json'if role=='latest-master'else R/'historical'/role/str(seed)/'receipt.json';v=json.loads(p.read_text());log=p.with_name('stdout.log');assert hashlib.sha256(log.read_bytes()).hexdigest()==v['stdout_sha256'];assert v['seed']==seed and v['allocation']==list(range(32))
  assert math.isclose(v['total_cpu_seconds'],v['user_cpu_seconds']+v['system_cpu_seconds'],abs_tol=1e-9)
  if role=='latest-master':
   assert v['total_cpu_seconds']==v['raw_wait4_usage']['ru_utime']+v['raw_wait4_usage']['ru_stime'];assert math.isclose(v['e2e_seconds'],v['finished_monotonic']-v['started_monotonic'],abs_tol=1e-9)
  if v['status']=='winner':
   z=re.findall(r'^GLOB2_GAME_END ticks=(\\d+) winner_team=(-?\\d+) seed=(\\d+) ',log.read_text(),re.M);assert len(z)==1 and tuple(map(int,z[0]))==(v['ticks'],v['winner'],seed)
   rows.append({'e2e_seconds':v['e2e_seconds'],'total_cpu_seconds':v['total_cpu_seconds'],'e2e_tps':v['ticks']/v['e2e_seconds'],'cpu_ms_tick':1000*v['total_cpu_seconds']/v['ticks'],'peak_mib':v['peak_rss_kib']/1024,'ticks':v['ticks']})
 groups[role]=rows
s=json.loads((R/'results/summary.json').read_text())
for role,rows in groups.items():
 assert len(rows)==s['groups'][role]['winners']
 for key in rows[0]:assert math.isclose(statistics.mean(x[key]for x in rows),s['groups'][role]['metrics'][key]['mean'],rel_tol=1e-12),(role,key)
for c in s['comparisons']:
 if c['metric']in groups['latest-master'][0]:
  a=statistics.mean(x[c['metric']]for x in groups[c['reference']]);b=statistics.mean(x[c['metric']]for x in groups['latest-master']);assert math.isclose(100*(b/a-1),c['change_pct'],abs_tol=1e-9)
print('Verified',len(m['files']),'files, all96 raw summaries and reported primary metric means/deltas')
''')
(D/'README.md').write_text((O/'publication-summary.md').read_text())
for name in ['verify.py','README.md']:
 q=D/name;entries.append({'path':name,'bytes':q.stat().st_size,'sha256':sha(q)})
(D/'manifest.json').write_text(json.dumps({'files':entries,'omissions':['Native executable and object/cache files','Per-game disposable profiles and generated replay bodies','Full historical sources already retained under earlier evidence commit5903347157253813a6a328fd4a362d997f77c232'],'note':'Original absolute paths are provenance; verify.py is portable and recomputes raw summary math. Bootstrap and plotting source is retained with package versions.'},indent=2)+'\n')
subprocess.run(['python3',str(D/'verify.py')],check=True);print('Selected',len(entries),'files,',sum(x['bytes']for x in entries),'bytes')
