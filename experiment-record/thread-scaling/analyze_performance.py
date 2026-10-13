#!/usr/bin/env python3
"""Read completed, input-verified paired jobs only; never launch engine/build tools."""
import argparse
import datetime
import hashlib
import html
import json
import math
import os
from pathlib import Path
import random
import statistics

ART = Path(__file__).resolve().parent
METRICS = {'simulation_cpu_s':'Engine interval CPU', 'simulation_wall_s':'Engine run wall',
           'cpu_s':'Whole process CPU', 'wall_s':'End-to-end wall', 'peak_rss_bytes':'Process peak RSS'}
DRAWS = 4000

def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):
            h.update(chunk)
    return h.hexdigest()

def load(path):
    return json.loads(Path(path).read_text())

def dump(path,value):
    temp=path.with_suffix(path.suffix+'.tmp')
    temp.write_text(json.dumps(value,indent=2)+'\n')
    temp.replace(path)

def stats(seeds,metric):
    if not seeds:
        return dict(ratio=None,ci95=None,seed_count=0,paired_repeats=0)
    strata=[s['metrics'][metric]['paired_log_ratios'] for s in seeds.values()]
    mean=statistics.mean(statistics.mean(v) for v in strata)
    rng=random.Random(7134)
    values=[]
    for _ in range(DRAWS):
        sampled=rng.choices(strata,k=len(strata))
        values.append(math.exp(sum(sum(rng.choices(v,k=len(v)))/len(v) for v in sampled)/len(sampled)))
    values.sort()
    ci=[values[int(DRAWS*.025)],values[int(DRAWS*.975)-1]]
    return dict(ratio=math.exp(mean),percent_change=100*(math.exp(mean)-1),ci95=ci,
                percent_ci95=[100*(v-1) for v in ci],seed_count=len(seeds),
                paired_repeats=sum(map(len,strata)),bootstrap='Seed resample then within-seed paired-repeat resample; deterministic RNG7134;4000 draws',
                credible_over5percent_regression=metric in ('simulation_cpu_s','simulation_wall_s','cpu_s','wall_s') and ci[0]>1.05)

def aggregate_stats(cells, metric):
    if not cells:
        return dict(ratio=None, percent_change=None, ci95=None, layouts=0)
    strata=[[s['metrics'][metric]['paired_log_ratios'] for s in cell['seeds'].values()] for cell in cells]
    means=[sum(sum(v)/len(v) for v in seeds)/len(seeds) for seeds in strata]
    ratio=math.exp(sum(means)/len(means))
    rng=random.Random(7134);samples=[]
    for _ in range(DRAWS):
        layout_means=[]
        for seeds in strata:
            selected=rng.choices(seeds,k=len(seeds))
            layout_means.append(sum(sum(rng.choices(v,k=len(v)))/len(v) for v in selected)/len(selected))
        samples.append(math.exp(sum(layout_means)/len(layout_means)))
    samples.sort();ci=[samples[int(DRAWS*.025)],samples[int(DRAWS*.975)-1]]
    return dict(ratio=ratio,percent_change=100*(ratio-1),ci95=ci,percent_ci95=[100*(v-1) for v in ci],
                layouts=len(cells),incomplete_layouts_excluded=True,
                scope='Fixed listed layouts weighted equally; seed then paired-repeat bootstrap within each layout, no resampling of map population')

def read_job(job):
    directory=Path(job['output']);passed=directory/'campaign-passed.json';auditpath=directory/'input-verification.json'
    if not passed.exists():return None,'not passed/completed'
    seal=load(passed);audit=load(auditpath)
    if not seal.get('passed') or audit.get('state')!='verified' or not audit.get('inputs_unchanged'):
        return None,'input audit not verified'
    if any(seal['identity'].get(k)!=v for k,v in job['identity'].items()):
        return None,'worklist/pass identity differs'
    repeats=job.get('repeats',seal['identity'].get('repeats',3))
    records=[json.loads(line) for line in (directory/'measurements.jsonl').read_text().splitlines()]
    pairs={}
    for row in records:
        if row['repeat']<0:continue
        if row['variant'] not in ('baseline','candidate') or row['repeat'] not in range(repeats):
            raise ValueError('unexpected paired sample label: '+job['id'])
        if row['variant'] in pairs.setdefault(row['repeat'],{}):raise ValueError('duplicate paired sample')
        pairs[row['repeat']][row['variant']]=row
    if set(pairs)!=set(range(repeats)) or any(set(p)!= {'baseline','candidate'} for p in pairs.values()):
        raise ValueError('incomplete paired samples under passed marker: '+job['id'])
    manifest=load(job['manifest']);s=manifest['scenarios'][0]
    end=int(s['args'][s['args'].index('--ticks')+1]);start=s['start_tick']
    for p in pairs.values():
        for row in p.values():
            if row['result']['ticks']!=end or row['result']['benchmark_measured_ticks']!=end-start or row['result']['termination']!='tick_cap':
                raise ValueError('window mismatch under passed marker: '+job['id'])
    metrics={}
    for metric in METRICS:
        logs=[math.log(pairs[r]['candidate'][metric]/pairs[r]['baseline'][metric]) for r in range(repeats)]
        metrics[metric]=dict(paired_log_ratios=logs,
            baseline_values=[pairs[r]['baseline'][metric] for r in range(repeats)],
            candidate_values=[pairs[r]['candidate'][metric] for r in range(repeats)],
            baseline_median=statistics.median(p[metric] for pair in pairs.values() for name,p in pair.items() if name=='baseline'),
            candidate_median=statistics.median(p[metric] for pair in pairs.values() for name,p in pair.items() if name=='candidate'))
    # Native process CPU/tick and engine CPU/tick are separate numerator scopes.
    return dict(job=job['id'],output=str(directory),seed=job['seed'],start_tick=start,end_tick=end,measured_ticks=end-start,
                affinity=seal['identity'].get('affinity',job.get('mask',job.get('slot'))),metrics=metrics,
                cpu_per_tick_s={name: [pairs[r][name]['cpu_s']/(end-start) for r in range(repeats)] for name in ('baseline','candidate')},
                engine_cpu_per_tick_s={name: [pairs[r][name]['simulation_cpu_s']/(end-start) for r in range(repeats)] for name in ('baseline','candidate')},
                provenance=dict(pass_sha256=digest(passed),audit_sha256=digest(auditpath),measurements_sha256=digest(directory/'measurements.jsonl'))),None

def render(result):
    def metric(cell):
        if cell['ratio'] is None:return '—'
        lo,hi=cell['percent_ci95']
        css='regression' if cell['credible_over5percent_regression'] else ''
        return '<span class="'+css+'">'+f"{cell['percent_change']:+.1f}% [{lo:+.1f}, {hi:+.1f}]"+'</span>'
    rows=[]
    for factor,data in result['factors'].items():
        for layout,cell in data['layouts'].items():
            n=cell['completed_seed_count'];expected=cell['expected_seed_count']
            rows.append('<tr><td>'+html.escape(factor)+'</td><td>'+html.escape(layout)+f'</td><td>{n}/{expected}</td>'+''.join('<td>'+metric(cell['metrics'][m])+'</td>' for m in METRICS)+'</tr>')
    flags=''.join('<li>'+html.escape(f"{f['factor']} / {f['layout']} / {f['metric']}: {f['percent_change']:+.2f}% (descriptive95% lower bound {f['lower_percent']:+.2f}%)")+'</li>' for f in result['regressions']) or '<li>No >5% lower-bound CPU/wall regression among currently completed cells.</li>'
    negative=''.join('<li>'+html.escape(k)+': '+html.escape(v)+'</li>' for k,v in result['worker_explicit3_control'].items())
    return '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Core optimization evidence review</title><style>body{font:15px/1.5 system-ui;margin:30px;color:#21304b}table{border-collapse:collapse;font-size:13px}td,th{padding:7px;text-align:left;border-bottom:1px solid #ddd}th{position:sticky;top:0;background:white}.note{background:#fff4cd;padding:16px;max-width:1000px}.regression{color:#a22222;font-weight:bold}code{word-break:break-all}</style><h1>Core optimization: paired release evidence</h1>''' + f"<p>{result['completed_jobs']}/{result['expected_jobs']} jobs complete and input-verified as of {result['generated_utc']}.</p>" + '''<p class="note">''' + ('Complete initial matrix; acceptance still pending mature/full-game/platform/compatibility review.' if result['complete'] else 'PROVISIONAL, INCOMPLETE, NOT ACCEPTED. Missing jobs/seeds are shown explicitly; this page uses no unfinished measurements.') + '''</p><p>Each cell gives candidate/baseline percent change and a descriptive seed/repeat bootstrap interval. Equal weight per seed, then per completed layout for factor aggregates. Two seeds limit inference; intervals are exploratory across many comparisons, without multiplicity correction; repeat intervals do not establish coverage of unmeasured maps, maturity or platforms.</p><h2>Recommendations remain conditional</h2><p>The complete initial matrix supports the general worker eligibility policy and telemetry representation as the strongest candidates: auto controls improve scheduling and replay storage lowers process RSS. It does not establish a universal telemetry speedup. The large-mixed telemetry control slows CPU and wall by about6.6%/6.3% (descriptive intervals exclude zero) and needs focused mature or held-out follow-up despite no strict&gt;5% lower-bound alert. Snapshot and optional scheduler timings are broadly flat; retain them only for justified ownership/simplicity benefits after lifetime/maturity review, without speed claims. Scheduler stage4 remains unaccepted.</p><h2>Preserve features; simplify mechanisms</h2><p>Worker eligibility is a platform scheduling/default policy; explicit three-worker runs are a negative control. Snapshot changes shorten unused-reference lifetimes while retaining asynchronous searches. Replay metadata sharing retains every ordered value, timestamp and exact AIT1 bytes. Scheduler wake filtering remains an optional, unaccepted stage: assess simpler ownership and general workload behavior before integration. GPU-on cases need source/backend-health context; an enabled switch does not prove healthy sustained device execution.</p><p>GPU health audit: only224 of338,304 requested fields completed on GPU across24 measured GPU-on records (0.066%);338,080 completed on CPU,337,578 requests reported unavailable and80 coordinator fallbacks cited cancellation. Driver-fallback counters were zero and error strings empty. These are mostly CPUfallback cases with device startup activity, not sustained GPU acceleration evidence. See <a href="gpu-initial-health.json">all native health counters</a>; no production code was instrumented for this audit.</p><h2>Measurement boundaries</h2><p>Whole process CPU and end-to-end wall include process startup, loading, canonical replay writing and teardown. Engine CPU covers prepareRun/beginSession through finishSession and final gradient drain; engine run wall uses a different boundary. Peak RSS includes initial loading and all later phases. These start-at-zero fixed windows cannot establish mature/full-winner memory or performance. Four parallel slots use fixed three-core/six-SMT allocations; each paired repeat stays on one slot. This cohort differs from the historical legacy workload.</p><h2>Completed-cell regressions requiring investigation</h2><ul>''' + flags + '''</ul><h2>Explicit three-worker negative control</h2><ul>''' + negative + '''</ul><h2>Per-factor, per-layout evidence</h2><table><thead><tr><th>Factor</th><th>Layout</th><th>Seeds</th>''' + ''.join('<th>'+html.escape(v)+'</th>' for v in METRICS.values()) + '''</tr></thead><tbody>''' + ''.join(rows) + '''</tbody></table><p>Static aggregate plots, when available: <a href="performance-initial.svg">SVG</a> / <a href="performance-initial.png">PNG</a>.</p><p><a href="performance-analysis.json">Exact pairs, missing IDs, intervals and input provenance</a> · <a href="performance/worklist.json">Sealed worklist</a> · <a href="correctness/complete.json">Correctness campaign status</a></p></html>'''

def plots(result):
    if not result['complete']:return dict(status='withheld until full matrix complete')
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:return dict(status='matplotlib unavailable; use existing historical plotting environment, no dependencies changed')
    factors=[(k,v) for k,v in result['factors'].items() if '-gpuoff' in k]
    fig,axes=plt.subplots(1,3,figsize=(15,5),layout='constrained')
    for ax,metric in zip(axes,('cpu_s','wall_s','peak_rss_bytes')):
        values=[v['aggregate_complete_layouts'][metric] for _,v in factors]
        x=list(range(len(factors)));y=[v['percent_change'] for v in values]
        ax.barh(x,y,xerr=[[v['percent_change']-v['percent_ci95'][0] for v in values], [v['percent_ci95'][1]-v['percent_change'] for v in values]],capsize=3,color=['#308666' if n<0 else '#a84444' for n in y])
        ax.set_yticks(x,[k.replace('-gpuoff','') for k,_ in factors]);ax.axvline(0,color='#555',lw=1)
        ax.set_title(METRICS[metric]);ax.set_xlabel('Candidate change (%)');ax.grid(axis='x',alpha=.2)
    fig.suptitle('Initial frozen-window matrix; equal layout/seed weight; acceptance pending')
    for suffix in ('svg','png'):fig.savefig(ART/f'performance-initial.{suffix}',dpi=160)
    plt.close(fig)
    return dict(status='rendered',files=['performance-initial.svg','performance-initial.png'])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--plots',action='store_true');args=p.parse_args()
    os.sched_setaffinity(0,{15,31})
    jobs=load(ART/'performance/worklist.json');factors={};missing=[];complete=0
    for job in jobs:
        group=factors.setdefault(job['factor'],{'layouts':{}})
        cell=group['layouts'].setdefault(job['scenario'],dict(expected_seeds=[],seeds={}))
        cell['expected_seeds'].append(job['seed'])
        value,reason=read_job(job)
        if value is None:missing.append(dict(job=job['id'],reason=reason));continue
        complete+=1;cell['seeds'][str(job['seed'])]=value
    regressions=[];negative={}
    for factor,group in factors.items():
        completed=[]
        for layout,cell in group['layouts'].items():
            cell['expected_seed_count']=len(cell['expected_seeds']);cell['completed_seed_count']=len(cell['seeds'])
            cell['complete']=cell['expected_seed_count']==cell['completed_seed_count']
            cell['metrics']={m:stats(cell['seeds'],m) for m in METRICS}
            if cell['complete']:completed.append(cell)
            for metric,estimate in cell['metrics'].items():
                if estimate.get('credible_over5percent_regression'):
                    regressions.append(dict(factor=factor,layout=layout,metric=metric,percent_change=estimate['percent_change'],lower_percent=estimate['percent_ci95'][0],provisional=not cell['complete']))
            if factor=='worker-t3-gpuoff':
                ci=cell['metrics']['cpu_s']['ci95']
                negative[layout]=('missing n0' if ci is None else 'consistent within±5% on CPU' if ci[0]>=.95 and ci[1]<=1.05 else 'interval overlaps no change; precision insufficient for±5% equivalence' if ci[0]<=1<=ci[1] else 'non-flat descriptive CPU result; inspect workload/noise/policy before mechanism attribution')+f"; n{cell['completed_seed_count']}/{cell['expected_seed_count']}"
        group['complete_layout_count']=len(completed)
        # Factor aggregate gives equal weight to full two-seed layouts only.
        group['aggregate_complete_layouts']={}
        for metric in METRICS:
            group['aggregate_complete_layouts'][metric]=aggregate_stats(completed,metric)
    result=dict(generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),expected_jobs=len(jobs),completed_jobs=complete,complete=complete==len(jobs),accepted=False,factors=factors,missing=missing,regressions=regressions,worker_explicit3_control=negative,source_sha256=digest(Path(__file__)),bootstrap_seed=7134,bootstrap_draws=DRAWS)
    if args.plots:result['plots']=plots(result)
    dump(ART/'performance-analysis.json',result)
    (ART/'optimization-report.html').write_text(render(result))
    print(json.dumps(dict(completed=complete,expected=len(jobs),complete=result['complete'],regression_flags=len(regressions),missing=len(missing))))
if __name__=='__main__':main()
