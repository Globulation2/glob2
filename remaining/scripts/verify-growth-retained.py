from pathlib import Path
import subprocess,json,sys
from xml.etree import ElementTree as E
root=Path.cwd();out=root/'artifacts/resource-growth/remaining'
suites=[s.attrib['name'] for s in E.parse(out/'broad.xml').getroot()]+['AIPipeline','AIOrderScheduler','SceneExtract','SceneBuffer','MapSets','*Stream*','*Replay*','MatchSetup']
for name,binary,filters in [('retained-broad','engine',['--exclude-tag','golden']+sum((['--filter',s+'/*'] for s in suites),[])),('retained-unit','unit',['--filter','ComputeExecutor/*','--filter','*Stream*/*','--filter','*Version*/*','--filter','*Replay*/*']),('retained-golden','engine',['--tag','golden'])]:
 cmd=[sys.executable,'test/run_tests.py','--binary',binary,'--no-display','-j','4','--artifacts',str(out/name),'--junit',str(out/(name+'.xml')),*filters]
 (out/(name+'-command.json')).write_text(json.dumps({'cwd':str(root),'command':cmd},indent=2))
 with (out/(name+'.log')).open('w') as f:run=subprocess.run(cmd,cwd=root,stdout=f,stderr=subprocess.STDOUT)
 print(name,run.returncode,flush=True)
