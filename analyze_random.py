import json,collections,statistics,pathlib,importlib.util
root=pathlib.Path(__file__).resolve().parent
rows=[json.loads(l) for l in (root/'random-r19.jsonl').open()]
old={r['seed']:r for r in map(json.loads,(root/'random-final.jsonl').open())}
assert len(rows)==2000 and len({r['seed'] for r in rows})==2000
assert {r['seed'] for r in rows}==set(range(100000,102000))
for r in rows:
 assert all(r[k]==old[r['seed']][k] for k in ['w','h','teams','set'])
 assert 'm' in r and 'parse' not in r
sp=importlib.util.spec_from_file_location('study',root/'study.py');s=importlib.util.module_from_spec(sp);sp.loader.exec_module(s)
domains,_=s.controls(str(root/'candidate19-glob2'),'portage-lakes')
observed={k:sorted({r['set'][k] for r in rows}) for k in domains}
assert observed==domains
attempts=[r['m'].get('tel:portage-lakes.landscape.selected') for r in rows if r['ok']]
times=sorted(r['seconds'] for r in rows)
failures=[{k:r[k] for k in ['seed','w','h','teams','set','detail','exit_code']} for r in rows if not r['ok'] or r['exit_code']!=0]
data=dict(requests=len(rows),failures=failures,unique_request_seeds=2000,exact_requests_match_prior_study=True,all_registered_control_values_covered=True,control_values=observed,shapes=sorted({(r['w'],r['h']) for r in rows}),teams=sorted({r['teams'] for r in rows}),workers=sorted({r['set']['workers'] for r in rows}),selected_landscape_counts=dict(collections.Counter(attempts)),resown_maps=sum('tel:portage-lakes.farm.resown' in r['m'] for r in rows),earlier_failed_seeds=[r['seed'] for r in old.values() if not r['ok']],wall_seconds=dict(median=statistics.median(times),p95=times[int(.95*(len(times)-1))],maximum=max(times)),source_sha256=json.load(open(root/'provenance.json'))['files']['candidate19-source.cpp'])
(root/'reliability-final.json').write_text(json.dumps(data,indent=2)+'\n')
(root/'reliability-final.md').write_text(f'''# Final randomized reliability study

{len(rows)} distinct requests, {len(failures)} failures, no missing reports, no duplicate seeds. Exact seed/settings pairs match the earlier candidate17 run. All16 ordered64–512 shapes, team counts1–12 within the area cap, worker counts1–8, and every registered value of all eight generator controls occurred.

All four earlier failing seeds ({', '.join(map(str,data['earlier_failed_seeds']))}) are included. The worst successful selected landscape index is {max(attempts)} of24 attempts. Re-sowing was used in {data['resown_maps']} selected maps. Final source SHA-256: `{data['source_sha256']}`.

The one-machine Linux pool used deterministic request RNG seed20260919 and map seeds100000–101999. Exact commands/settings and per-map results are retained in random-r19.jsonl; binary hashes are in provenance.json. The first448 rows completed in blocks, then the same request list continued with a continuously fed native pool; completed requests were verified and not repeated. This is one2,000-map cohort, not two studies counted together.

Wall time per map under shared concurrent load: median{data['wall_seconds']['median']:.3f}s, p95{data['wall_seconds']['p95']:.3f}s, max{data['wall_seconds']['maximum']:.3f}s. These are workload timings; use performance-r19.jsonl for CPU benchmarks.

Zero observed failures is sample evidence, not a proof for every possible seed/parameter combination. The separate436-request envelope and777-request control studies cover intentional boundaries and paired knob effects. AI games and their late-game limits are reported separately.
''')
print(json.dumps(data,indent=2))
