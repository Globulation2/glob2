import json
from pathlib import Path
root=Path(__file__).resolve().parent
import sys
controlled='--controlled' in sys.argv
m=json.loads((root/('engine-controlled' if controlled else 'engine-timing')/'metadata.json').read_text())['manifest']
for s in m['scenarios']:
 old=s['fixture_sha256']
 paths={k:str(root/('controlled-fixtures' if controlled else 'fixtures')/s['id']/'initial.game.gz') for k in old}
 s['args']=[paths.get(a,a) for a in s['args']]
 s['fixture_sha256']={paths[k]:v for k,v in old.items()}
print(json.dumps(m,indent=2))
