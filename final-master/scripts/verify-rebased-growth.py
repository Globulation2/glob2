from pathlib import Path
import subprocess,json,sys
root=Path.cwd();b=root/'artifacts/resource-growth/final-master'
for name,binary,filters in [('integration-final','engine',['--coverage-profile','compatibility','--exclude-tag','golden']),('golden-update','engine',['--tag','golden','--update-fixtures'])]:
 cmd=[sys.executable,'test/run_tests.py','--binary',binary,'--no-display','-j','4','--artifacts',str(b/name),'--junit',str(b/(name+'.xml')),*filters]
 (b/(name+'-command.json')).write_text(json.dumps({'cwd':str(root),'command':cmd},indent=2)+'\n')
 with (b/(name+'.log')).open('w') as f:result=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
 print(name,result.returncode,flush=True)
 if result.returncode:sys.exit(result.returncode)
