import json,pathlib,re
root=pathlib.Path('artifacts/ai-rules/qualification-accepted');r=json.loads((root/'qualification.json').read_text());audit=[];no_work=[];training=[]
for game in r['results']:
 log=(root/game['name']/'run.log').read_text()
 rows=[]
 for line in log.splitlines():
  if line.startswith('GLOB2_AI_FINAL '):
   fields=dict(re.findall(r'(\w+)=([\w.-]+)',line));
   if fields.get('active')=='1' and fields.get('available')=='1':
    rows.append({k:fields[k] for k in ['player','ai','tick','polls','emitted_orders']})
    if int(fields['emitted_orders'])==0:no_work.append(game['name']+':'+fields['player'])
  if game['rules'].get('noUpgrades') and line.startswith('GLOB2_MEASURE '):
   for key,value in re.findall(r'(trainingVisits_\d+)=(\d+)',line):
    if int(value):training.append([game['name'],key,value])
 audit.append({'game':game['name'],'rules':game['rules'],'ticks':game['result']['ticks'],'controllers':rows,'unresolved':game['result']['unresolved']})
summary={'games':r['games'],'native_games':r['native_games'],'javascript_games':r['javascript_games'],'failures':r['failures'],'controllers_with_no_emitted_work':no_work,'disabled_training_visit_violations':training,'ticks_simulated':sum(g['result']['ticks'] for g in r['results']),'profiles':sorted(set(g['profile'] for g in r['results'])),'games_detail':audit}
pathlib.Path('artifacts/ai-rules/qualification-analysis.json').write_text(json.dumps(summary,indent=2));print({k:v for k,v in summary.items() if k!='games_detail'})
