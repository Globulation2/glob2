from pathlib import Path
import subprocess,json,sys
from xml.etree import ElementTree as E
root=Path.cwd();out=root/'artifacts/resource-growth/remaining'
suites=[s.attrib['name'] for s in E.parse(out/'broad.xml').getroot()]
for name,filters in [('production-broad',sum((['--filter',s+'/*'] for s in suites),[])),('production-golden',['--tag','golden'])]:
 cmd=[sys.executable,'test/run_tests.py','--binary','engine','--no-display','-j','4','--artifacts',str(out/name),'--junit',str(out/(name+'.xml')),*filters]
 (out/(name+'-command.json')).write_text(json.dumps(cmd,indent=2))
 with (out/(name+'.log')).open('w') as f:subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,check=True)
 print(name,'pass',flush=True)
