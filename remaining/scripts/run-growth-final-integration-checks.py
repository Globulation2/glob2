from pathlib import Path
import subprocess,json,sys,os
from xml.etree import ElementTree as E
root=Path.cwd();b=root/'artifacts/resource-growth/remaining'
suites=[s.attrib['name'] for s in E.parse(b/'retained-broad.xml').getroot()]+['JavaScriptCompatibility','GameHeaderTextSaveLoad','BuildingCatalog','BuildingArtwork','BuildingLibrary','BuildingPackages','BuildingFamilyLinks','ExperimentalFeatures']
for name,binary,filters in [('final-integration-broad','engine',['--exclude-tag','golden']+sum((['--filter',s+'/*'] for s in dict.fromkeys(suites)),[])),('final-integration-unit','unit',['--filter','ComputeExecutor/*','--filter','*Stream*/*','--filter','*Version*/*','--filter','*Replay*/*','--filter','GameHeaderTextSaveLoad/*'])]:
 cmd=[sys.executable,'test/run_tests.py','--binary',binary,'--no-display','-j','4','--artifacts',str(b/name),'--junit',str(b/(name+'.xml')),*filters]
 (b/(name+'-command.json')).write_text(json.dumps({'cwd':str(root),'command':cmd},indent=2))
 with (b/(name+'.log')).open('w') as f:r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
 print(name,r.returncode,flush=True)
 if r.returncode:sys.exit(r.returncode)
