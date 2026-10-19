from pathlib import Path
import os,sys,json,collections,hashlib
P=Path('/home/bradley/glob2/artifacts/gradient-unified-20261019/round-005/cumulative-1280-research');sys.path.insert(0,str(P/'analysis-tool-source-A16'))
from tools import adaptive_gradient_spatial as collector,adaptive_gradient_dataset as d
from tools.resource_gradient_dataset import manifest_records
inputs=json.loads((P/'terminal-inputs.json').read_text());mapping={};evidence=[]
for name in inputs['manifests']:
 records,metadata=manifest_records(P/name)
 for record in records:
  spec=record['spec'];roster=spec.get('ais') or [spec['ai']]*spec['players'];assert len(roster)==spec['players'];identity='|'.join((record['group'],record['binary_sha256'],record['episode_sha256']))
  if identity in mapping:assert mapping[identity]==roster
  mapping[identity]=roster
rows=collector.load([P/n for n in inputs['manifests']]);eligible=d.eligible(rows)
for r in rows:
 roster=mapping[r['collection']];assert 0<=r['team']<len(roster);r['seat_ai']=roster[r['team']]
coverage={}
for ai in sorted({r['seat_ai'] for r in rows}):
 coverage[ai]={}
 for family in d.FAMILIES:
  allr=[r for r in rows if r['seat_ai']==ai and r['family']==family];sub=[r for r in allr if r['capture_known'] and r['movement_label_available']];coverage[ai][family]=dict(complete_episodes=len(allr),eligible_episodes=len(sub),collections=len({r['collection'] for r in sub}),maps=len({r['group'] for r in sub}),episode_movement_queries=sum(r['queries'] for r in sub),episode_planning_queries=sum(r['planning_queries'] for r in sub),positive_direction_benefit_rows=sum(r['defer_extra']>r['refresh_extra'] for r in sub),negative_direction_benefit_rows=sum(r['defer_extra']<r['refresh_extra'] for r in sub),zero_direction_benefit_rows=sum(r['defer_extra']==r['refresh_extra'] for r in sub),defer_false_unreachable=sum(r['defer_false_unreachable'] for r in sub),refresh_false_unreachable=sum(r['refresh_false_unreachable'] for r in sub),defer_missed_moves=sum(r['defer_missed_moves'] for r in sub),refresh_missed_moves=sum(r['refresh_missed_moves'] for r in sub))
out=dict(status='PASS-ARTIFACT-ONLY',input_sha256=hashlib.file_digest((P/'terminal-inputs.json').open('rb'),'sha256').hexdigest(),complete_rows=len(rows),eligible_rows=len(eligible),attribution='Exactepisodefieldteam->sameindexactualspec.ais using frozenmapcollectionCLI one--playerperteam; Headless BasePlayer(p,players[p],p,AI) assigns teamNumber=p. No whole-roster attribution.',caveats=['Episodequerycountsarecounterfactualfuturehorizonobservationsandmayoverlap,notuniquelivegamequeries','AISpecificfamilyobservationsdonotprovewhichAIordercreatedtheareaorwhichalliedunitusedit','Marketsunmeasurednomappedrows'],coverage=coverage)
Path(__file__).with_name('AI-FAMILY-COVERAGE.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({ai:v['guard'] for ai,v in coverage.items()}))
