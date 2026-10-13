#!/usr/bin/env python3
"""Read saved control records only; create a partial-aware report. Never launch games/builds."""
from pathlib import Path
import datetime, hashlib, html, json, math, os, random, re, statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parent
PHASES = [('full', 'full.json'), ('screen', 'screen.json'),
          ('prototype-full', 'prototype-full.json'), ('oazis-window', 'oazis-windows.json')]
METRICS = ['wall_seconds', 'run_wall_seconds', 'total_cpu_seconds', 'user_cpu_seconds',
           'system_cpu_seconds', 'cpu_seconds_per_tick', 'peak_rss_kib',
           'voluntary_context_switches', 'involuntary_context_switches', 'max_observed_threads']

def read(path, default=None):
    try: return json.loads(path.read_text())
    except (FileNotFoundError, json.JSONDecodeError): return default

def sha(path):
    with path.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()

def variant(case):
    prefix = 'prototype ' if case.get('binary') else 'original '
    return f"{prefix}t{case['threads']} / GPU {case['gpu']}"

def pct(values, q):
    ordered = sorted(values); at = (len(ordered)-1)*q; lo = int(at)
    return ordered[lo] + (ordered[min(lo+1, len(ordered)-1)]-ordered[lo])*(at-lo)

def pair_summary(pairs, metric):
    # Preserve repetitions within their seed; bootstrap whole seed clusters.
    groups = {}
    for base, candidate in pairs:
        a, b = base.get(metric), candidate.get(metric)
        if a is not None and b is not None and a > 0 and b > 0:
            groups.setdefault(candidate['seed'], []).append(math.log(b/a))
    if not groups: return {'matched_pairs': 0, 'seed_clusters': 0}
    seed_means = {str(seed): statistics.mean(logs) for seed, logs in groups.items()}
    center = statistics.mean(seed_means.values())
    result = dict(matched_pairs=sum(map(len, groups.values())), seed_clusters=len(groups),
                  geometric_ratio=math.exp(center), change_percent=100*math.expm1(center),
                  per_seed_change_percent={seed:100*math.expm1(x) for seed,x in seed_means.items()})
    if len(groups) >= 3:
        rng = random.Random(5102026); values = list(seed_means.values())
        boot = [100*math.expm1(statistics.mean(rng.choices(values,k=len(values)))) for _ in range(10000)]
        result['seed_cluster_bootstrap_95_percent'] = [pct(boot,.025),pct(boot,.975)]
        result['interval_qualification'] = f'Descriptive only: {len(groups)} independent seeds; repetitions are not independent clusters. Seed coverage and shared-host variation limit population inference.'
    else:
        result['interval_qualification'] = 'No interval: fewer than three matched seed clusters.'
    return result

def enriched(record, directory):
    record = dict(record)
    text = (directory/'stdout.log').read_text(errors='replace') if (directory/'stdout.log').exists() else ''
    checks = re.findall(r'nox::gui\.game\.checkSum\(\)\s*=\s*([0-9a-fA-F]+)', text)
    record['stdout_final_checksum'] = checks[-1].lower() if checks else None
    record['raw_result'] = str((directory/'result.json').relative_to(ROOT))
    record['stdout'] = str((directory/'stdout.log').relative_to(ROOT))
    record['finished_with_winner'] = record.get('returncode') == 0 and int(record.get('reported',{}).get('winner',-1)) >= 0
    record['valid_timing'] = record.get('returncode') == 0 and (record.get('ticks_actual') or 0)>0 and not any(record.get(flag) for flag in ['diagnostic','trace','instrumented','timing_excluded','timed_out'])
    return record

def concordance(records):
    grouped = {}
    for record in records: grouped.setdefault((record['seed'],record.get('repetition',1)),[]).append(record)
    out = []
    for (seed,rep), group in sorted(grouped.items()):
        values = [(x.get('ticks_actual'), x.get('reported',{}).get('winner'), x.get('reported',{}).get('ended'), x.get('stdout_final_checksum')) for x in group]
        out.append(dict(seed=seed,repetition=rep,cases=[x['id'] for x in group],
                        observations=[dict(case=x['id'],ticks=x.get('ticks_actual'),winner=x.get('reported',{}).get('winner'),ended=x.get('reported',{}).get('ended'),final_checksum=x.get('stdout_final_checksum')) for x in group],
                        ticks_match=len({v[0] for v in values})==1 if len(values)>1 else None,
                        winners_match=len({v[1] for v in values})==1 if len(values)>1 else None,
                        final_checksums_match=(len({v[3] for v in values})==1) if len(values)>1 and all(v[3] is not None for v in values) else None,
                        final_checksum_available=all(v[3] is not None for v in values),
                        qualification='Final-state agreement only; full per-tick checksum equivalence belongs to the separate diagnostic cohort. Singleton groups establish no comparison.'))
    return out

def gather():
    output = dict(generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  timing_phases={}, excluded_records=[], observations={},
                  historical_data='Original engine-history and engine-history-oazis records/binaries untouched.',
                  host_context='Controls pinned to CPUs12–14,28–30; analysis15,31. Four background Oazis games use disjoint CPUs0–11,16–27. No assigned-CPU overlap, but shared memory bandwidth/cache, GPU, temperature and OS activity can contend.',
                  method='Match candidate/base by seed and repetition. Geometric paired ratios average log ratios within each seed, then equally across seeds. Bootstrap resamples whole seed clusters; three seeds is limited coverage, not strong population inference.')
    plans = {}
    for phase, filename in PHASES:
        plans[phase] = read(ROOT/filename,[]) or []
    records = [enriched(read(p,{}),p.parent) for p in sorted((ROOT/'runs').glob('*/result.json')) if read(p)]
    for record in records:
        if not record.get('valid_timing') or record.get('phase') not in plans:
            output['excluded_records'].append(record)
    for phase, filename in PHASES:
        plan = plans[phase]; ids={x['id'] for x in plan}
        rows=[x for x in records if x.get('phase')==phase and x.get('id') in ids and x['valid_timing']]
        variants=sorted({variant(x) for x in plan})
        baseline='original t6 / GPU off' if phase=='prototype-full' else 'original tauto / GPU on'
        base={(x['seed'],x.get('repetition',1)):x for x in rows if variant(x)==baseline}
        summaries=[]
        for label in variants:
            group=[x for x in rows if variant(x)==label]
            pairs=[(base[(x['seed'],x.get('repetition',1))],x) for x in group if (x['seed'],x.get('repetition',1)) in base]
            means={metric:statistics.mean(x[metric] for x in group if x.get(metric) is not None) if any(x.get(metric) is not None for x in group) else None for metric in METRICS}
            summaries.append(dict(variant=label,planned=sum(variant(x)==label for x in plan),completed=len(group),seed_count=len({x['seed'] for x in group}),means=means,
                                  mean_cpu_wall_ratio=statistics.mean(x['total_cpu_seconds']/x['wall_seconds'] for x in group) if group else None,
                                  paired={metric:pair_summary(pairs,metric) for metric in METRICS},cases=[x['id'] for x in group]))
        contrasts=[]
        for reference,candidate in [('original t4 / GPU off','original t4 / GPU on'),('original tauto / GPU off','original tauto / GPU on'),('original t6 / GPU off','prototype tauto / GPU off'),('original t4 / GPU off','original t3 / GPU off'),('original t6 / GPU off','original tauto / GPU off')]:
            if reference not in variants or candidate not in variants:continue
            lookup={(x['seed'],x.get('repetition',1)):x for x in rows if variant(x)==reference}
            pairs=[(lookup[(x['seed'],x.get('repetition',1))],x) for x in rows if variant(x)==candidate and (x['seed'],x.get('repetition',1)) in lookup]
            contrasts.append(dict(reference=reference,candidate=candidate,metrics={metric:pair_summary(pairs,metric) for metric in METRICS}))
        missing=[x['id'] for x in plan if x['id'] not in {r['id'] for r in rows}]
        output['timing_phases'][phase]=dict(plan=filename,planned=len(plan),completed=len(rows),partial=bool(missing),missing=missing,baseline=baseline,variants=summaries,direct_contrasts=contrasts,concordance=concordance(rows),records=rows,
                                          end_scope='Run to winner, one-hour stop' if phase in ['full','prototype-full'] else '8192-tick window; not a completed-match estimate')
    diagnostic=read(ROOT/'checksum-verification.json')
    if diagnostic: output['observations']['legacy_checksum_verification']=diagnostic
    structured=read(ROOT/'structured-summary.json')
    if structured:
        output['observations']['structured_warning']=structured.get('warning')
        output['observations']['structured_comparisons']=structured.get('comparisons',[])
        probes=structured.get('records',[])
    else:
        probes=[read(p,{}) for p in sorted((ROOT/'structured-probes').glob('*/probe-result.json'))]
    output['observations']['structured_probes']=probes
    output['input_hashes']={str(p.relative_to(ROOT)):sha(p) for p in [ROOT/n for _,n in PHASES] if p.exists()}
    output['prototype_provenance']=read(ROOT/'affinity-auto/build.json')
    output['partial']=any(x['partial'] for x in output['timing_phases'].values())
    return output

def display(value,kind='number'):
    if value is None:return '—'
    if kind=='ratio':return f'{value:+.1f}%'
    if kind=='rss':return f'{value/1024:.1f}'
    if kind=='tick':return f'{value*1000:.3f}'
    return f'{value:,.2f}' if isinstance(value,float) else str(value)

def table(headers,rows):
    return '<table><thead><tr>'+''.join('<th>'+html.escape(str(x))+'</th>' for x in headers)+'</tr></thead><tbody>'+''.join('<tr>'+''.join('<td>'+html.escape(str(x))+'</td>' for x in row)+'</tr>' for row in rows)+'</tbody></table>'

def plot(data):
    fig,axes=plt.subplots(4,3,figsize=(15,14));fig.suptitle('Latest-engine controls — uninstrumented cohorts only\nPARTIAL' if data['partial'] else 'Latest-engine controls — completed uninstrumented cohorts')
    specs=[('cpu_seconds_per_tick','Process CPU per tick (ms)',1000),('wall_seconds','End-to-end wall (s)',1),('peak_rss_kib','Peak host RSS (MiB)',1/1024)]
    for i,(phase,_) in enumerate(PHASES):
        summaries=[x for x in data['timing_phases'][phase]['variants'] if x['completed']]
        labels=[x['variant'].replace('original ','').replace('prototype ','POC ') for x in summaries]
        for j,(metric,title,factor) in enumerate(specs):
            ax=axes[i,j];ax.set_title(f'{phase}: {title}');ax.set_ylabel(title)
            if not summaries:ax.text(.5,.5,'Awaiting records',ha='center',transform=ax.transAxes);ax.set_xticks([]);continue
            ax.bar(range(len(summaries)),[x['means'][metric]*factor for x in summaries],color=['#356c9b' if not x['variant'].startswith('prototype') else '#ab7340' for x in summaries])
            # Points are observed runs, not independent-seed error bars.
            rows=data['timing_phases'][phase]['records']
            for k,x in enumerate(summaries):
                values=[r[metric]*factor for r in rows if variant(r)==x['variant'] and r.get(metric) is not None]
                ax.scatter([k]*len(values),values,c='#222',s=12,zorder=3)
            ax.set_xticks(range(len(labels)),labels,rotation=45,ha='right',fontsize=8);ax.grid(axis='y',alpha=.2)
    fig.tight_layout(rect=(0,0,.99,.95));fig.savefig(ROOT/'summary.png',dpi=150);plt.close(fig)

def render(data):
    status='PARTIAL — planned cases remain' if data['partial'] else 'All planned timing cases complete'
    parts=['<!doctype html><meta charset="utf-8"><title>Engine control experiments</title><style>body{font:15px system-ui;max-width:1450px;margin:32px auto;padding:0 24px;color:#253344}table{border-collapse:collapse;margin:16px 0;width:100%;font-size:13px}th,td{border:1px solid #d8dce2;padding:7px;text-align:right}th:first-child,td:first-child{text-align:left}th{background:#edf2f7}img{max-width:100%}.warn{padding:16px;background:#fff4d6}code{font-size:12px}a{color:#27679b}</style>',f'<h1>Engine controls: {status}</h1>',f'<p>Generated {data["generated_utc"]}. Reproduce with <code>../engine-history/.venv/bin/python analyze_controls.py</code>. The analyzer reads saved records and launches no games or builds.</p>',f'<p class="warn">{data["historical_data"]} {data["host_context"]} Timing tables exclude checksum/diagnostic and strace cases. Window tests do not estimate complete-game time.</p>',f'<p>{data["method"]} Means below can use different available subsets while partial; use matched paired changes for comparisons. Dots show individual repetitions, not independent confidence intervals.</p>','<img src="summary.png" alt="CPU per tick, wall time and memory by control cohort">']
    for phase,_ in PHASES:
        block=data['timing_phases'][phase];parts+=[f'<h2>{phase}: {block["completed"]}/{block["planned"]} valid timing records</h2>',f'<p>{block["end_scope"]}. Paired reference: {block["baseline"]}. <a href="{block["plan"]}">Raw plan</a>.</p>']
        rows=[]
        for x in block['variants']:
            m=x['means'];rows.append([x['variant'],f"{x['completed']}/{x['planned']}",x['seed_count'],display(m['wall_seconds']),display(m['run_wall_seconds']),display(m['total_cpu_seconds']),display(m['user_cpu_seconds']),display(m['system_cpu_seconds']),display(m['cpu_seconds_per_tick'],'tick'),display(m['peak_rss_kib'],'rss'),display(m['max_observed_threads']),display(m['voluntary_context_switches']),display(m['involuntary_context_switches'])])
        parts.append(table(['Variant','Runs','Seeds','E2E s','Engine s','CPU s','User s','System s','CPU/tick ms','RSS MiB','Observed threads max mean','Voluntary switches','Involuntary switches'],rows))
        rows=[]
        for x in block['variants']:
            for metric in ['wall_seconds','total_cpu_seconds','cpu_seconds_per_tick','peak_rss_kib','voluntary_context_switches','involuntary_context_switches']:
                p=x['paired'][metric]
                if not p['matched_pairs']:continue
                interval=p.get('seed_cluster_bootstrap_95_percent');ci='['+', '.join(f'{v:+.1f}%' for v in interval)+'] descriptive only' if interval else 'No CI: <3 seeds'
                rows.append([x['variant'],metric,p['matched_pairs'],p['seed_clusters'],display(p['change_percent'],'ratio'),ci,'; '.join(f'{s}: {v:+.1f}%' for s,v in p['per_seed_change_percent'].items())])
        parts.append(table(['Candidate','Metric','Pairs','Seeds','Paired change','95% cluster bootstrap','Per-seed paired changes'],rows))
        direct=[]
        for c in block['direct_contrasts']:
            for metric in ['wall_seconds','total_cpu_seconds','system_cpu_seconds','peak_rss_kib','voluntary_context_switches']:
                pair=c['metrics'][metric]
                if pair['matched_pairs']:direct.append([c['reference'],c['candidate'],metric,pair['matched_pairs'],pair['seed_clusters'],display(pair['change_percent'],'ratio')])
        if direct:parts.append('<h3>Direct matched controls</h3>'+table(['Reference','Candidate','Metric','Pairs','Seeds','Paired change'],direct))
        parts.append('<details><summary>Outcome, tick and final checksum concordance</summary>'+table(['Seed','Rep','Compared cases','Ticks match','Winners match','Final checksum match'],[[x['seed'],x['repetition'],len(x['cases']),x['ticks_match'],x['winners_match'],x['final_checksums_match']] for x in block['concordance']])+'<p>Singleton groups establish no comparison. The legacy driver prints a final nox checksum at the tick cap, but completed-winner logs may omit it: None means unavailable, not disagreement. Final checksums alone do not prove every tick identical; exact trace checks belong to diagnostics. Full per-case winners, ended flags, ticks and hashes are retained in summary.json.</p>'+table(['Case','Seed','Rep','Ticks','Winner','Ended','Final checksum'],[[r['id'],r['seed'],r.get('repetition',1),r.get('ticks_actual'),r.get('reported',{}).get('winner'),r.get('reported',{}).get('ended'),r.get('stdout_final_checksum')] for r in block['records']])+'</details>')
        parts.append('<details><summary>Raw timing records</summary><ul>'+''.join(f'<li><a href="{x["raw_result"]}">{x["id"]}</a> · <a href="{x["stdout"]}">stdout</a></li>' for x in block['records'])+'</ul></details>')
    parts+=['<h2>Separate instrumented observations</h2>','<p>Diagnostic, strace and structured game-run probes are excluded from all timing means and ratios. Structured games have no legacy observer and collect checksum/gradient-stat/device diagnostics. Counter normalization describes work; it does not supply a benchmark speed estimate.</p>']
    probes=data['observations'].get('structured_probes',[]);rows=[]
    for r in probes:
        case=r.get('case',{});c=r.get('counters') or {};g=c.get('gpu_execution',{});ai=c.get('snapshot_ai',{})
        snapshots={k:v for k,v in ai.items() if any(w in k.lower() for w in ['snapshot','bytes','copy'])}
        rows.append([case.get('id'),r.get('success'),r.get('max_observed_threads'),display(r.get('peak_rss_kib'),'rss'),g.get('device_dispatches'),g.get('gpu_complete_fields'),display(g.get('coordinator_initialization_ns',0)/1e6),display(g.get('accelerator_peak_host_bytes',0)/1024,'rss'),display(g.get('accelerator_peak_device_bytes',0)/1024,'rss'),json.dumps(snapshots)])
    parts.append(table(['Probe','Success','Observed threads','RSS MiB','Device dispatches','GPU complete fields','Initialization ms','Repo host peak MiB','Repo device peak MiB','Snapshot counters'],rows) if rows else '<p>Structured probes are pending.</p>')
    parts.append('<p>GPU initialization may happen before useful dispatch. Repository payload counters exclude driver/context/compiler memory; they cannot alone explain process RSS. One-thread GPU-on bypasses service initialization, so worker counts must match when isolating GPU memory. <a href="gpu-memory.txt">GPU ownership investigation</a> · <a href="scheduler.txt">Scheduler investigation</a> · <a href="kernel-plan.txt">Kernel investigation</a>.</p>')
    parts.append('<details><summary>Excluded diagnostic/trace raw records</summary><ul>'+''.join(f'<li><a href="{r["raw_result"]}">{r["id"]}</a>: diagnostic={r.get("diagnostic")}, trace={r.get("trace")}, phase={r.get("phase")}</li>' for r in data['excluded_records'])+'</ul></details>')
    links=['structured-summary.json','checksum-verification.json','structured-plan.json','affinity-auto/README.txt','affinity-auto/build.json','affinity-auto/affinity-auto.patch','affinity-auto/helper-results.json','../engine-history/analysis/middle.txt','../engine-history/report.html','../engine-history-oazis/report.html','summary.json']
    parts+=['<h2>Provenance and limits</h2><p>Prototype auto sizing is an incremental experimental binary; preserve all game/engine features. Its exact patch and build/dependency hashes are linked below. No production release qualification is implied. Screens use one seed and one run; full cohorts have three seeds and two repetitions. Historical performance remains separate.</p>','<ul>'+''.join(f'<li><a href="{x}">{x}</a></li>' for x in links if (ROOT/x).exists())+'</ul>', '<h3>Input plan SHA-256</h3>'+table(['Path','SHA-256'],data['input_hashes'].items())]
    (ROOT/'report.html').write_text('\n'.join(parts))

def main():
    os.sched_setaffinity(0,{15,31})
    data=gather();plot(data);render(data)
    (ROOT/'summary.json').write_text(json.dumps(data,indent=2)+'\n')
    print(json.dumps({p:{k:b[k] for k in ['completed','planned','partial']} for p,b in data['timing_phases'].items()},indent=2))

if __name__=='__main__':
    import sys
    if '--extended' in sys.argv:
        from analyze_extended import main as extended_main
        extended_main()
    else:main()
