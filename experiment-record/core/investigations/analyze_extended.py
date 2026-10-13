#!/usr/bin/env python3
"""Metadata-driven deeper-control report, separate from original timing reports."""
from pathlib import Path
import datetime,html,json,os,statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from analyze_controls import read,enriched,pair_summary,concordance,table,display,sha,METRICS
ROOT=Path(__file__).resolve().parent
PLANS={'kernel-window':'kernel-windows.json','autosave-window':'autosave-windows.json','pipeline-window':'pipeline-windows.json','depth-window':'depth-windows.json','feature-full':'feature-full.json','pool-window':'pool-windows.json','replay-window':'replay-windows.json','replay-cpu-window':'replay-cpu-windows.json','arena-window':'arena-windows.json','scheduler-window':'h03-lane-wake/h03-windows.json','sizing-full':'sizing-full.json','oazis-sizing-full':'oazis-sizing-full.json','replay-confirm-window':'replay-confirm-windows.json','scheduler-n3-window':'h03-lane-wake/n3-windows.json'}


def label(case):
    if case.get('variant') is not None:return str(case['variant'])
    if case.get('phase')=='pipeline-window':return f"AI{case['threads']}, gradients{case['gradient_workers']}"
    if case.get('phase')=='depth-window':return 'depth '+case['depth']
    return f"threads{case.get('threads')}, GPU{case.get('gpu')}"


def workload(case):
    return (case.get('phase'),case.get('map','Playground'),case.get('width',128),case.get('height',128),case.get('teams',8),case.get('controllers',case.get('teams',8)+1),case.get('ticks'),case.get('stratum',''))


def reference(phase,cases):
    explicit=next((c.get('reference_variant') for c in cases if c.get('reference_variant')),None)
    return explicit or {'kernel-window':'parent-kernel','autosave-window':'on','pipeline-window':'AI4, gradients2','depth-window':'depth table','feature-full':'default'}[phase]


def enrich(record,path):
    result=enriched(record,path.parent)
    native=read(Path(record['native_json'])) if record.get('native_json') else None
    if native:
        result['native_result']=native
        if result['stdout_final_checksum'] is None and native.get('finalChecksum') is not None:result['stdout_final_checksum']=f"{int(native['finalChecksum']):08x}";result['checksum_source']='native result finalChecksum'
    return result


def gather():
    plans={name:read(ROOT/file,[]) or [] for name,file in PLANS.items()};planned=[c for cases in plans.values() for c in cases];lookup={c['id']:c for c in planned};records=[];excluded=[];attempted=[]
    for path in sorted((ROOT/'runs').glob('*/result.json')):
        value=read(path)
        if not value or value.get('id') not in lookup:continue
        # Planned metadata defines workload/variant. Do not infer it from binary/name syntax.
        value={**lookup[value['id']],**value};value['phase']=lookup[value['id']]['phase']
        r=enrich(value,path)
        if r.get('full_game') and not r['finished_with_winner']:r['valid_timing']=False
        attempted.append(r)
        if not r['valid_timing']:excluded.append(r)
        else:records.append(r)
    groups={}
    for c in planned:groups.setdefault(workload(c),[]).append(c)
    output=[]
    for key,cases in sorted(groups.items(),key=lambda x:str(x[0])):
        phase,mapname,width,height,teams,controllers,ticklimit,stratum=key;ids={c['id'] for c in cases};rows=[r for r in records if r['id'] in ids];allrows=[r for r in attempted if r['id'] in ids];capped=[r for r in allrows if r.get('timed_out')];baseline=reference(phase,cases);bases={(r['seed'],r.get('repetition',1)):r for r in rows if label(r)==baseline};variants=[]
        for variant in sorted({label(c) for c in cases}):
            runs=[r for r in rows if label(r)==variant];pairs=[(bases[(r['seed'],r.get('repetition',1))],r) for r in runs if (r['seed'],r.get('repetition',1)) in bases]
            means={m:statistics.mean(r[m] for r in runs if r.get(m) is not None) if any(r.get(m) is not None for r in runs) else None for m in METRICS}
            attempts=[r for r in allrows if label(r)==variant]
            variants.append(dict(variant=variant,planned=sum(label(c)==variant for c in cases),attempted=len(attempts),completed=len(runs),capped=sum(bool(r.get('timed_out')) for r in attempts),means=means,paired={m:pair_summary(pairs,m) for m in METRICS},cases=[r['id'] for r in runs]))
        output.append(dict(phase=phase,map=mapname,stratum=stratum,width=width,height=height,teams=teams,controllers=controllers,tick_limit=ticklimit,reference=baseline,planned=len(cases),attempted=len(allrows),completed=len(rows),capped=len(capped),partial=len(allrows)<len(cases),variants=variants,concordance=concordance(rows),records=rows,capped_records=capped,excluded_records=[r for r in allrows if not r['valid_timing']]))
    return dict(generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),partial=any(g['partial'] for g in output),groups=output,excluded_records=excluded,plan_hashes={file:sha(ROOT/file) for file in PLANS.values() if (ROOT/file).exists()},qualification='Original historical/control records remain unchanged. Compare matching workloads, seeds and repetitions. Native 8-player cohorts differ from legacy 8 AI plus observer. Diagnostic and instrumented runs are excluded. One-hour caps count as attempted, censored runs and are excluded from completed-game means and paired speed ratios. Missing ticks are unknown. Completion-only comparisons can select easier seeds when variants have different cap rates. Separate CPU groups prevent assigned CPU overlap; memory, cache, GPU, thermal and OS activity remain shared.',source_plans=PLANS)


def plot(data):
    count=max(1,len(data['groups']));fig,axes=plt.subplots(count,3,figsize=(15,max(4,count*3.3)),squeeze=False)
    for i,g in enumerate(data['groups']):
        for j,(metric,title,factor) in enumerate([('cpu_seconds_per_tick','CPU/tick ms',1000),('wall_seconds','E2E wall s',1),('peak_rss_kib','Peak RSS MiB',1/1024)]):
            ax=axes[i,j];ax.set_title(f"{g['phase']} / {g['map']} {g['stratum']} ({g['completed']}/{g['planned']}): {title}")
            rows=[r for r in g['variants'] if r['completed']]
            if not rows:ax.text(.5,.5,'Pending',ha='center',transform=ax.transAxes);ax.set_xticks([]);continue
            ax.bar(range(len(rows)),[r['means'][metric]*factor for r in rows],color='#3779a8')
            for k,row in enumerate(rows):
                values=[r[metric]*factor for r in g['records'] if label(r)==row['variant'] and r.get(metric) is not None];ax.scatter([k]*len(values),values,c='#222',s=12,zorder=3)
            ax.set_xticks(range(len(rows)),[r['variant'] for r in rows],rotation=35,ha='right',fontsize=8);ax.grid(axis='y',alpha=.2)
    fig.suptitle('Deeper engine controls — '+('PARTIAL' if data['partial'] else 'planned timing cohorts complete'));fig.tight_layout(rect=(0,0,1,.97));fig.savefig(ROOT/'extended-summary.png',dpi=140);plt.close(fig)


def report(data):
    parts=['<!doctype html><meta charset="utf-8"><title>Deeper engine controls</title><style>body{font:15px system-ui;max-width:1500px;margin:32px auto;padding:0 24px;color:#263746}table{border-collapse:collapse;width:100%;font-size:12px;margin:18px 0}td,th{padding:7px;border:1px solid #ddd;text-align:right}td:first-child,th:first-child{text-align:left}th{background:#edf3f8}img{max-width:100%}</style>',f"<h1>Deeper controls: {'PARTIAL' if data['partial'] else 'timing cohorts complete'}</h1>",f"<p>{data['generated_utc']}. {data['qualification']}</p>",'<p>Variants come from case metadata; filenames/binary overrides do not define experiment labels. Map/tick/controller workloads remain separate. Paired ratios match seed and repetition. Three-seed bootstrap resamples whole seed clusters; repetitions are not independent seeds. Means on partial subsets can differ in seed composition.</p>','<img src="extended-summary.png" alt="Separate deeper control workload plots">']
    for g in data['groups']:
        parts+=[f"<h2>{g['phase']} — {g['map']} {g['width']}×{g['height']}, {g['teams']} teams/{g['controllers']} controllers: {g['completed']} completed, {g['capped']} capped, {g['attempted']}/{g['planned']} attempted {html.escape(g['stratum'])}</h2>",f"<p>Tick limit {g['tick_limit']}; full-game cohorts run to winner or a one-hour wall cap. Paired reference {html.escape(g['reference'])}. <a href=\"{PLANS[g['phase']]}\">Raw plan</a>.</p>"]
        rows=[]
        for v in g['variants']:
            m=v['means'];rows.append([v['variant'],f"{v['attempted']}/{v['planned']}",v['completed'],v['capped'],display(m['wall_seconds']),display(m['total_cpu_seconds']),display(m['user_cpu_seconds']),display(m['system_cpu_seconds']),display(m['cpu_seconds_per_tick'],'tick'),display(m['peak_rss_kib'],'rss'),display(m['max_observed_threads']),display(m['voluntary_context_switches']),display(m['involuntary_context_switches'])])
        parts.append(table(['Variant','Attempted','Completed','Capped','Completed wall s','CPU s','User s','System s','CPU/tick ms','RSS MiB','Observed threads mean','Voluntary switches','Involuntary switches'],rows));paired=[]
        for v in g['variants']:
            for m in ['wall_seconds','total_cpu_seconds','cpu_seconds_per_tick','peak_rss_kib']:
                p=v['paired'][m]
                if not p['matched_pairs']:continue
                ci=p.get('seed_cluster_bootstrap_95_percent');interval='['+', '.join(f'{x:+.1f}%' for x in ci)+f"] {p['seed_clusters']} seed clusters; descriptive" if ci else 'NoCI:<3matchedseeds'
                paired.append([v['variant'],m,p['matched_pairs'],p['seed_clusters'],display(p['change_percent'],'ratio'),interval,json.dumps(p['per_seed_change_percent'])])
        parts.append(table(['Candidate','Metric','Pairs','Seeds','Pairedchange','Cluster95% interval','Perseedchanges'],paired))
        if g['capped_records']:
            parts.append('<h3>One-hour censored runs</h3><p>Wall and CPU below are consumed before termination, not completed-game estimates. Unknown final ticks and winners remain unknown.</p>'+table(['Case','Wall consumed s','CPU consumed s','Peak RSS MiB','Ticks observed','Raw'],[[r['id'],display(r.get('wall_seconds')),display(r.get('total_cpu_seconds')),display(r.get('peak_rss_kib'),'rss'),r.get('ticks_actual'),r['raw_result']] for r in g['capped_records']]))
        parts.append('<details><summary>Outcome/checksum concordance and rawrecords</summary>'+table(['Seed','Rep','Cases','Ticksmatch','Winnersmatch','Finalchecksums match'],[[c['seed'],c['repetition'],len(c['cases']),c['ticks_match'],c['winners_match'],c['final_checksums_match']] for c in g['concordance']])+'<p>None means absent/singleton comparison. Completed-winner legacy logs may omitfinalchecksum. Finalstatehash alone doesnotestablishper-tick equality; separatechecksumdiagnostics provide thatqualification.</p>'+table(['Case','Ticks','Winner','Ended','Finalchecksum','Raw'],[[r['id'],r.get('ticks_actual'),r.get('reported',{}).get('winner'),r.get('reported',{}).get('ended'),r.get('stdout_final_checksum'),r['raw_result']] for r in g['records']])+'<ul>'+''.join(f'<li><a href="{r["raw_result"]}">{r["id"]}</a> · <a href="{r["stdout"]}">stdout</a></li>' for r in g['records'])+'</ul></details>')
    parts.append('<h2>Separate diagnostic/instruction/allocation evidence</h2><p>Counter observations,heaptrack/Callgrind timings and synthetickernelmeasurements are not mixedwithgamewindow/fullmatch timings. Instructions are notCPUcycles. Allocationchurn isnotretainedheap/RSS. Kerneloraclevalidatesfixtureoutputs butnotwholeengineplatformcoverage.</p><ul>')
    for path in ['report.html','summary.json','kernel-341/build.json','kernel-341/micro-summary.json','kernel-checks.json','heap-census-summary.json','callgrind-summary.json','structured-summary.json','gpu-reachability.txt','heap-census-older.json']:
        if (ROOT/path).exists():parts.append(f'<li><a href="{path}">{path}</a></li>')
    parts+=['</ul>', '<h2>Plan SHA-256</h2>'+table(['Plan','SHA-256'],data['plan_hashes'].items())]
    (ROOT/'extended-report.html').write_text('\n'.join(parts))


def main():
    os.sched_setaffinity(0,{15,31});data=gather();plot(data);report(data);(ROOT/'extended-summary.json').write_text(json.dumps(data,indent=2)+'\n');print(json.dumps({f"{g['phase']}/{g['map']}/{g['stratum']}":f"{g['completed']}/{g['planned']}" for g in data['groups']},indent=2))
if __name__=='__main__':main()
