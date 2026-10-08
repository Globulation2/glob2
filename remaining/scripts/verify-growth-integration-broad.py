from pathlib import Path
import subprocess,json,sys
from xml.etree import ElementTree as E
root=Path.cwd();out=root/'artifacts/resource-growth/remaining';cwd=out/'integration-src'
suites=[s.attrib['name'] for s in E.parse(out/'broad.xml').getroot()]+['AIPipeline','AIOrderScheduler','SceneExtract','SceneBuffer']
for name,binary,filters in [('integration-broad','engine',sum((['--filter',s+'/*'] for s in suites),[])),('integration-golden','engine',['--tag','golden']),('integration-executor','unit',['--filter','ComputeExecutor/*'])]:
 cmd=[sys.executable,'test/run_tests.py','--binary',binary,'--no-display','-j','4','--artifacts',str(out/name),'--junit',str(out/(name+'.xml')),*filters]
 (out/(name+'-command.json')).write_text(json.dumps({'cwd':str(cwd),'command':cmd},indent=2))
 with (out/(name+'.log')).open('w') as f:subprocess.run(cmd,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,check=True)
 print(name,'pass',flush=True)
