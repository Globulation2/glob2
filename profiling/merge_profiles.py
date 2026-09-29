import pathlib,subprocess,collections,json
root=pathlib.Path(__file__).resolve().parent
merged=collections.Counter(); records={}
for path in sorted(root.glob('*.perf.data')):
    report=subprocess.check_output(['sudo','-n','perf','report','--stdio','--no-children','--show-total-period','--field-separator=;','--percent-limit=0','-i',str(path)],text=True)
    (root/(path.stem+'.csv')).write_text(report)
    counts=collections.Counter()
    for line in report.splitlines():
        if line.startswith('#'): continue
        parts=line.split(';',4)
        if len(parts)!=5: continue
        counts[parts[4].strip()]+=int(parts[1])
    records[path.name]={'total_period':sum(counts.values()),'symbols':dict(counts)}
    merged.update(counts)
total=sum(merged.values())
out={'profiles':records,'total_period':total,'pooled':[{'symbol':s,'percent':100*v/total,'period':v,'equal_game_percent':sum(100*r['symbols'].get(s,0)/r['total_period'] for r in records.values())/len(records)} for s,v in merged.most_common()]}
(root/'merged-profiles.json').write_text(json.dumps(out,indent=2))
for row in out['pooled'][:35]: print(f"{row['percent']:6.2f}% pooled {row['equal_game_percent']:6.2f}% equal-game {row['symbol']}")
