from pathlib import Path
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
   z=re.findall(r'^GLOB2_GAME_END ticks=(\d+) winner_team=(-?\d+) seed=(\d+) ',log.read_text(),re.M);assert len(z)==1 and tuple(map(int,z[0]))==(v['ticks'],v['winner'],seed)
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
