"""Portable primary comparison audit; run beside manifest.json on evidence branch."""
from pathlib import Path
import hashlib,json,math,re,statistics
O=Path(__file__).resolve().parent
s=json.loads((O/'results/previous-master-comparison.json').read_text())
request=json.loads((O/'request.json').read_text())
proof={(r['role'],r['seed']):r for r in s['raw_audit']}
groups={}
for role,root in [('previous',O.parent/'playground-latest-987c95369/latest'),('current',O/'latest')]:
    rows=[]
    for seed in request['seeds']:
        p=root/str(seed)/'receipt.json';r=json.loads(p.read_text());log=p.with_name('stdout.log')
        assert hashlib.sha256(p.read_bytes()).hexdigest()==proof[role,seed]['receipt_sha256']
        assert hashlib.sha256(log.read_bytes()).hexdigest()==proof[role,seed]['stdout_sha256']==r['stdout_sha256']
        assert r['seed']==seed and r['allocation']==request['CPU_affinity'] and r['status']=='winner'
        v=re.findall(r'^GLOB2_GAME_END ticks=(\d+) winner_team=(\d+) seed=(\d+) map="Playground" orders=(\d+)',log.read_text(),re.M)
        assert len(v)==1 and tuple(map(int,v[0][:3]))==(r['ticks'],r['winner'],seed)
        u=r['raw_wait4_usage'];assert r['total_cpu_seconds']==u['ru_utime']+u['ru_stime']
        assert math.isclose(r['e2e_seconds'],r['finished_monotonic']-r['started_monotonic'],abs_tol=1e-9)
        rows.append(dict(e2e_seconds=r['e2e_seconds'],e2e_tps=r['ticks']/r['e2e_seconds'],total_cpu_seconds=r['total_cpu_seconds'],cpu_ms_tick=1000*r['total_cpu_seconds']/r['ticks'],peak_mib=r['peak_rss_kib']/1024,ticks=r['ticks'],orders=int(v[0][3])))
    groups[role]=rows
for key,m in s['metrics'].items():
    a=statistics.mean(r[key]for r in groups['previous']);b=statistics.mean(r[key]for r in groups['current'])
    assert math.isclose(a,m['previous_mean'],rel_tol=1e-12)
    assert math.isclose(b,m['current_mean'],rel_tol=1e-12)
    assert math.isclose(100*(b/a-1),m['change_pct'],abs_tol=1e-9)
print('Verified primary comparison:64 raw native summaries, receipt/log hashes, CPU/wall and seven metric means/deltas')
