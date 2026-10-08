from pathlib import Path
import subprocess,os,json
r=Path.cwd();b=r/'artifacts/resource-growth/final-integration';env=dict(os.environ,LD_LIBRARY_PATH='/tmp/glob2-sdl3/prefix/lib')
def run(name,args):
    (b/(name+'-command.json')).write_text(json.dumps(args,indent=2)+'\n')
    with (b/(name+'.log')).open('w') as log:
        code=subprocess.run(args,env=env,stdout=log,stderr=subprocess.STDOUT).returncode
    print(name,code,flush=True)
    return code
assert run('refresh-golden',['python3','test/run_tests.py','--binary','engine','--no-display','-j4','--tag','golden','--update-fixtures','--artifacts',str(b/'refresh-golden'),'--junit',str(b/'refresh-golden.xml')])==0
exe=str(r/'build/linux/client/release/src/glob2')
for name,script in [('maxima','test/maxima/check_save_continuation_fixture.py'),('legacy','test/check_telemetry_simulation.py')]:
    if run(name+'-before',['python3',script,exe,'--output',str(b/(name+'-before'))]):
        assert run('refresh-'+name,['python3',script,exe,'--output',str(b/('refresh-'+name)),'--update-fixtures'])==0
