"""Compare pinned current release with the preceding 32-seed master measurement."""
from pathlib import Path
import hashlib,html,json,math,re,runpy
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

O=Path(__file__).resolve().parent
P=O.parent/'playground-pr1194-f56cbc8f6'
def read(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    helper=runpy.run_path(str(O/'run.py'),run_name='helpers')
    assert not helper['census']()
    groups={};proof=[]
    req=read(O/'request.json');oldreq=read(P/'request.json')
    for k in ['seeds','map_sha256','CPU_affinity','compute_threads','gradient_backend','concurrent_games','cap_seconds']:
        assert req[k]==oldreq[k],k
    for role,root in [('previous',P),('current',O)]:
        end=read(root/'complete.json');build=read(root/'build-complete.json')
        assert end['state']=='complete' and end['runs']==32
        assert sha(root/'request.json')==end['request_sha256']
        assert sha(build['binary'])==build['binary_sha256']==end['binary_sha256']
        for lib,digest in build['shared_library_hashes'].items():assert sha(Path(lib))==digest
        assert build['shared_library_hashes']==read(O/'build-complete.json')['shared_library_hashes']
        games=[]
        for i,seed in enumerate(req['seeds']):
            p=root/'runs'/str(seed)/'receipt.json';r=read(p)
            assert sha(p)==end['receipts'][str(p.relative_to(root))]
            assert r['revision']==build['revision'] and r['binary_sha256']==build['binary_sha256']
            assert r['seed_index']==i and r['seed']==seed and r['map_sha256']==req['map_sha256']
            assert r['allocation']==req['CPU_affinity'] and r['compute_requested']=='auto' and r['compute_participants_preflight']==32
            log=p.with_name('stdout.log');assert sha(log)==r['stdout_sha256']
            parsed=helper['parse'](log.read_text(),seed,r['status']=='cap',r['returncode'])
            assert (parsed['status'],parsed['ticks'],parsed['winner'])==(r['status'],r['ticks'],r['winner'])
            assert r['checksum_files']==[] and r['telemetry']=='off'
            expected=['taskset','-c',','.join(map(str,req['CPU_affinity'])),build['binary'],'dev','random-games','--runs','1','--ticks','2147483647','--map','Playground','--matchup',','.join(['nicowar']*8),'--compute-threads','auto','--gradient-backend','cpu']
            assert r['command']==expected
            assert r['env']['GLOB2_TEST_SEED']==str(seed) and r['env']['GLOB2_TEST_MAX_TICKS']=='2147483647'
            u=r['raw_wait4_usage'];assert r['user_cpu_seconds']==u['ru_utime'] and r['system_cpu_seconds']==u['ru_stime'] and r['peak_rss_kib']==u['ru_maxrss']
            assert math.isclose(r['total_cpu_seconds'],u['ru_utime']+u['ru_stime'],abs_tol=1e-9)
            assert math.isclose(r['e2e_seconds'],r['finished_monotonic']-r['started_monotonic'],abs_tol=1e-9)
            r['e2e_tps']=r['ticks']/r['e2e_seconds'] if r['ticks'] else None
            r['cpu_ms_tick']=1000*r['total_cpu_seconds']/r['ticks'] if r['ticks'] else None
            r['peak_mib']=r['peak_rss_kib']/1024
            r['orders']=int(parsed['parsed']['orders']) if parsed.get('parsed') else None
            games.append(r);proof.append({'role':role,'seed':seed,'receipt_sha256':sha(p),'stdout_sha256':sha(log)})
        groups[role]=games
    out=O/'results';out.mkdir(exist_ok=True)
    metrics={'e2e_tps':('End-to-end tick rate','ticks/s'),'e2e_seconds':('Game completion time','s'),'total_cpu_seconds':('Total CPU per game','CPU-s'),'cpu_ms_tick':('CPU per tick','ms'),'peak_mib':('Peak memory per game','MiB'),'ticks':('Simulated game length','ticks'),'orders':('Recorded orders','orders')}
    pairs=[(a,b) for a,b in zip(groups['previous'],groups['current']) if a['status']==b['status']=='winner']
    n=len(pairs);assert n>0
    idx=np.random.default_rng(20261015).integers(0,n,size=(10000,n))
    comparisons={}
    for key in metrics:
        x=np.array([a[key] for a,b in pairs]);y=np.array([b[key] for a,b in pairs])
        boot=100*(y[idx].mean(1)/x[idx].mean(1)-1)
        comparisons[key]={'previous_mean':float(x.mean()),'current_mean':float(y.mean()),'change_pct':float(100*(y.mean()/x.mean()-1)),'paired_95pct':np.percentile(boot,[2.5,97.5]).tolist(),'previous_median':float(np.median(x)),'current_median':float(np.median(y))}
    ends={'winner_and_tick_matching':sum((a['winner'],a['ticks'])==(b['winner'],b['ticks']) for a,b in pairs),'winner_matching':sum(a['winner']==b['winner'] for a,b in pairs),'current_longer':sum(b['ticks']>a['ticks'] for a,b in pairs),'current_shorter':sum(b['ticks']<a['ticks'] for a,b in pairs)}
    result={'previous_revision':oldreq['revision'],'current_revision':req['revision'],'paired_completed_seeds':n,'completion_counts':{role:{'winners':sum(r['status']=='winner' for r in games),'caps':sum(r['status']=='cap' for r in games)}for role,games in groups.items()},'metrics':comparisons,'outcome_agreement':ends,'maximum_peak_mib':{role:max(r['peak_mib']for r in games)for role,games in groups.items()},'total_cpu_seconds':{role:sum(r['total_cpu_seconds']for r in games)for role,games in groups.items()},'limits':['Historical controls were not rerun or interleaved; paired bootstrap intervals describe seed variability, not environment drift.','Checksum-only fix on the same PR simulation version; winner/tick agreement is checked separately and is not full-state equivalence proof.','32 fixed seeds, one host, CPU backend, unrestricted auto32, serial whole games. No concurrent-throughput or platform-wide claim.','No timed checksum telemetry. Native winners/ticks are outcome observations, not per-tick equivalence.','Golden trace and 64-tick auto smoke are separate checks, not full platform qualification.'],'raw_audit':proof,'script_sha256':sha(Path(__file__))}
    (out/'previous-pr-comparison.json').write_text(json.dumps(result,indent=2)+'\n')
    body=['<!doctype html><meta charset="utf-8"><title>PR1194 bug-fix comparison</title><style>body{font:16px system-ui;max-width:1100px;margin:40px auto;line-height:1.5}table{border-collapse:collapse;width:100%}td,th{padding:10px;text-align:right;border-bottom:1px solid #ddd}td:first-child,th:first-child{text-align:left}img{width:100%}</style><h1>Playground: PR1194 bug-fix comparison</h1><p>32 fixed seeds, 128×128 Playground, eight Nicowars, one unrestricted auto32 CPU game at a time. Release GCC13 builds; same commands, map bytes and shared library hashes.</p><p>Previous: <code>'+oldreq['revision']+'</code><br>Current: <code>'+req['revision']+'</code></p><table><tr><th>Mean per completed game</th><th>Previous</th><th>Current</th><th>Change</th><th>Paired 95% interval</th></tr>']
    for key,(name,unit) in metrics.items():
        c=comparisons[key];lo,hi=c['paired_95pct']
        body.append(f'<tr><td>{name} ({unit})</td><td>{c["previous_mean"]:,.3f}</td><td>{c["current_mean"]:,.3f}</td><td>{c["change_pct"]:+.2f}%</td><td>[{lo:+.2f}%, {hi:+.2f}%]</td></tr>')
    body.append('</table><p>Completion counts: '+html.escape(str(result['completion_counts']))+'. Winner and finishing tick match on '+str(ends['winner_and_tick_matching'])+'/'+str(n)+' paired completed seeds.</p><img src="previous-pr-comparison.svg"><h2>Limits</h2><ul>'+''.join('<li>'+html.escape(x)+'</li>'for x in result['limits'])+'</ul><p><a href="previous-pr-comparison.json">Results, intervals and raw audit</a> · <a href="index.html">Earlier historical references</a></p>')
    (out/'previous-pr-comparison.html').write_text(''.join(body))
    fig,axes=plt.subplots(2,3,figsize=(13,8))
    for ax,key in zip(axes.flat,list(metrics)[:6]):
        c=comparisons[key];vals=[c['previous_mean'],c['current_mean']]
        ax.bar(['Earlier PR f56cbc8f6','Fixed PR 403dfd099'],vals,color=['#77899b','#14896a'])
        ax.set_title(metrics[key][0]);ax.set_ylabel(metrics[key][1]);ax.grid(axis='y',alpha=.15);ax.set_axisbelow(True)
        for i,v in enumerate(vals):ax.text(i,v,f'{v:,.2f}',ha='center',va='bottom')
        ax.set_ylim(0,max(vals)*1.2)
    fig.suptitle('Playground · eight Nicowars · 32 fixed seeds per version\nUnrestricted auto32 CPU, one game at a time; historical controls, changed simulation versions')
    fig.tight_layout(rect=(0,0,1,.92))
    for ext in ['svg','png']:fig.savefig(out/('previous-pr-comparison.'+ext),dpi=160)
    plt.close(fig)
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
