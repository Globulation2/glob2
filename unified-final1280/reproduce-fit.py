"""Extract the published episode archives beside this script, then refit four heads over the selected twelve inputs.

Run from a checkout of the tested PR source using the recorded pinned environment.
Only the original manifest bytes and episode data are used; no censored tail is imputed.
"""
import json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parent
checkout=Path.cwd()
assert (checkout/'tools/adaptive_gradient_model.py').is_file(),'Run from the tested checkout root'
sys.path.insert(0,str(checkout))
inputs=json.loads((root/'terminal-inputs.json').read_text())
# Current production fit defaults to generic12; refuse a changed default rather
# than silently reproduce the rejected seven-feature ablation.
from tools.adaptive_gradient_dataset import SELECTED_FEATURES,FEATURES
assert tuple(SELECTED_FEATURES)==tuple(FEATURES) and len(FEATURES)==12
cmd=[sys.executable,'tools/adaptive_gradient_model.py','fit','--split-registry',str(root/'map-splits.json'),'--output',str(root/'reproduced-twelve.json'),'--collector-contract','unified-gradient-spatial-h128-v3','--expected-families','materials,guard,clear,forbidden','--threshold-milli','16000']
for name in inputs['manifests']:cmd.extend(['--manifest',str(root/name)])
subprocess.run(cmd,check=True)
expected=json.loads((root/'generic12-compact-candidate.json').read_text())
actual=json.loads((root/'reproduced-twelve.json').read_text())
for key in ['features','transforms','positive_probability','negative_probability','positive_mean','negative_mean']:
 assert actual[key]==expected[key],key
print('Exact selected features and four fitted integer heads match.')
