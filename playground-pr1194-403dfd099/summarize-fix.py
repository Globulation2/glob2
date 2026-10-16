from pathlib import Path
import json,html
O=Path(__file__).resolve().parent
request=json.loads((O/'request.json').read_text())
audit=json.loads((O/'independent-point-audit.json').read_text())
base=json.loads((O/'results/previous-master-comparison.json').read_text())
prior=json.loads((O/'results/previous-pr-comparison.json').read_text())
metrics=[('e2e_tps','End-to-end ticks/s'),('e2e_seconds','Completion time(s)'),('total_cpu_seconds','Total CPU/game(CPU-s)'),('cpu_ms_tick','CPU per tick(ms)'),('peak_mib','Mean peak RSS(MiB)'),('ticks','Game length(ticks)')]
lines=['# PR #1194: checksum-fix rerun','',f"Tested updated PR head `{request['revision']}` against previous PR `{request['previous_pr_revision']}` and exact master base `{request['base_revision']}`.",'','Same 32 fixed seeds, 128×128 Playground, 8 Nicowars plus the legacy passive observer, one unrestricted auto32 CPU game at a time, one-hour cap. All 32 games reached a winner; no caps. Compiler, release flags and linked library hashes match the prior runs.','','| Mean per game | Master 987c95369 | Previous PR f56cbc8f6 | Fixed PR 403dfd099 |','| --- | ---: | ---: | ---: |']
body=['<!doctype html><meta charset="utf-8"><title>PR #1194 checksum-fix rerun</title><style>body{font:16px system-ui;max-width:1150px;margin:40px auto;line-height:1.5}table{border-collapse:collapse;width:100%;margin:24px 0}td,th{padding:10px;text-align:right;border-bottom:1px solid #ddd}td:first-child,th:first-child{text-align:left}img{width:100%}</style><h1>PR #1194: checksum-fix rerun</h1><p>32 fixed seeds · Playground 128×128 · eight Nicowars · CPU backend · unrestricted auto32 · one game at a time. All 32 games finished with a winner; no caps.</p><p>The fix removes a reverse destination-registry lookup for queued gradient requests during routine checksumming. Tested head403dfd099, previous PR f56cbc8f6, master987c95369.</p><table><tr><th>Mean per game</th><th>Master</th><th>Previous PR</th><th>Fixed PR</th></tr>']
for key,label in metrics:
    values=[audit['groups'][r][key]for r in ['master','previous_pr','fixed_pr']]
    formatted=[f'{v:,.3f}'for v in values]
    lines.append('| '+label+' | '+' | '.join(formatted)+' |')
    body.append('<tr><td>'+html.escape(label)+'</td>'+''.join('<td>'+x+'</td>'for x in formatted)+'</tr>')
body.append('</table>')
for title,summary,filename in [('Fix versus previous PR',prior,'previous-pr-comparison'),('Fixed PR versus master',base,'previous-master-comparison')]:
    lines+=['','## '+title,'','| Metric | Change | Paired 95% interval |','| --- | ---: | ---: |']
    body.append('<h2>'+title+'</h2><table><tr><th>Metric</th><th>Change</th><th>Paired 95% interval</th></tr>')
    for key,label in metrics:
        m=summary['metrics'][key];lo,hi=m['paired_95pct'];change=f"{m['change_pct']:+.2f}%";ci=f'[{lo:+.2f}%, {hi:+.2f}%]'
        lines.append('| '+label+' | '+change+' | '+ci+' |')
        body.append('<tr><td>'+html.escape(label)+'</td><td>'+change+'</td><td>'+ci+'</td></tr>')
    body.append('</table><p><a href="'+filename+'.html">Detailed comparison</a> · <a href="'+filename+'.json">Raw audit and estimates</a></p><img src="'+filename+'.svg">')
    lines+=['',f'![{title}](results/{filename}.png)']
notes=['The checksum fix improves observed tick rate 6.58%, completion time 6.39% and CPU/tick 1.39% versus the previous PR. Peak RSS rises 2.00%. All 32 winners, finishing ticks and recorded order counts match the previous PR exactly. These observations are not full-state equivalence proof.',
       'Against master, the fixed PR has 10.75% higher tick rate, 12.21% higher CPU/tick and 5.85% lower mean peak RSS. Whole-game completion time is not clearly different. Its games contain 10.44% more ticks on average; 23 winners differ from master. Gradient timing and trajectories differ, so this is not a same-trajectory master comparison.',
       'Historical controls were not rerun or interleaved. Paired 10,000-draw intervals describe seed variability, not environment drift. One host/CPU/unrestricted auto32/serial regime only; no concurrent-throughput or GPU/platform-wide claim. No optional timing checksum or profiling telemetry. Golden trace and 64-tick auto32 smoke passed separately.']
lines+=['','## Interpretation and limits','']+notes+['','[Combined report](results/fix-overview.html) · [96-summary independent point audit](independent-point-audit.json) · [Manifest](manifest.json)','','The evidence retains 32 new compact receipts/logs, exact commands/build/source hashes, fixed map, smoke records, statistics/plotting scripts and two primary comparisons. Prior PR and master raw receipts are retained in adjacent folders on this evidence branch, with their hashes bound in comparison JSON. Generated replay bodies, executables, object/cache files and disposable profiles are omitted.','']
body.append('<h2>Interpretation and limits</h2>'+''.join('<p>'+html.escape(x)+'</p>'for x in notes))
(O/'publication-summary.md').write_text('\n'.join(lines))
(O/'results/fix-overview.html').write_text(''.join(body))
