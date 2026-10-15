from pathlib import Path
import json, subprocess, sys
root=Path(__file__).resolve().parents[2]
plan=json.loads((root/'artifacts/checksum-draft/focused-plan.json').read_text())
mode=sys.argv[1]
common=[sys.executable,'test/run_tests.py','--build-dir',plan['build_dir'],'--no-display','--exclude-tag','benchmark','--jobs','4']
if mode=='golden':
    args=common+['--binary','engine','--filter','TurnEngineHarness/the committed match record*','--update-fixtures']
else:
    args=common+['--binary',mode]
    for pattern in plan[mode+'_filters']:args+=['--filter',pattern]
args+=['--artifacts','artifacts/checksum-draft/'+mode,'--junit','artifacts/checksum-draft/'+mode+'.xml','--write-inventory','artifacts/checksum-draft/'+mode+'-inventory.json']
print(' '.join(args),flush=True)
raise SystemExit(subprocess.call(args,cwd=root))
