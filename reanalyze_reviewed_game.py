from pathlib import Path
import hashlib, json, sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.tournaments.model import job
from tools.tournaments.worker import outcome_only
from tools.tournaments.analysis import reanalyze
OUT=ROOT/'artifacts/win-probability'
result=json.loads((OUT/'reviewed-continuity-v190/original/result.json').read_text())
assert result['status']=='completed' and result['ticks']==1024
binary=ROOT/'build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2'
request=job('game',hashlib.sha256(binary.read_bytes()).hexdigest(),seeds={'game':19},
            config={'players':['cortex','cortex'],'win_probability_permille':950},
            labels={'format':'1v1','block':'real-seed-19','pair':'real-seed-19','subject_player':0})
root=OUT/'reviewed-result-reanalysis-v190';(root/'results').mkdir(parents=True,exist_ok=True)
(root/'experiment.json').write_text(json.dumps({'id':'real-engine-probability-contract','jobs':[request]}))
record=dict(id='original',host='local',category='success',job=request,result=outcome_only(result),artifacts=[])
(root/'results'/(request['id']+'.json')).write_text(json.dumps(record))
report=reanalyze(root,draws=100)
assert report['probability_scores'], 'the completed real game must produce probability scores'
assert all(s['scored_games']==s['games'] for s in report['probability_scores'])
assert all(s['win_probability_permille']==950 for s in report['probability_scores'])
assert all('win_probability_history' not in t for t in record['result']['teams'])
print(json.dumps(report['probability_scores'],indent=2))
