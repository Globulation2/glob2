from pathlib import Path
import json,os,subprocess
ROOT=Path(__file__).resolve().parents[3]
BINARY=ROOT/'build/linux/client/debug/dev-dev_fast-true-linker-auto/src/MapGeneratorGoldenTest'
WORK=Path(__file__).resolve().parent/'golden-cli-test'
(WORK/'test').mkdir(parents=True,exist_ok=True)
TABLE=WORK/'test/map-generator-golden.txt'
TABLE.write_text('# Isolated golden CLI contract\n')
env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
commands=[]
def run(*args):
    cmd=[str(BINARY),'profile',*args]
    r=subprocess.run(cmd,cwd=WORK,env=env,text=True,capture_output=True,timeout=120)
    commands.append(dict(command=cmd,exit_code=r.returncode,stdout=r.stdout,stderr=r.stderr))
    return r
assert run('--update','--only=uniform').returncode==0
truth=[line for line in TABLE.read_text().splitlines() if line and not line.startswith('#')]
assert len(truth)==8
lines=TABLE.read_text().splitlines()
for i,line in enumerate(lines):
    if line and not line.startswith('#'):
        fields=line.split();fields[-1]=str(int(fields[-1]) ^ 1);lines[i]=' '.join(fields);break
TABLE.write_text('\n'.join(lines)+'\n');before=TABLE.read_bytes()
observed=run('--print','--only=uniform')
assert observed.returncode==0,observed.stdout
assert sorted(line for line in observed.stdout.splitlines() if line.startswith('linux-x86_64 '))==sorted(truth)
assert TABLE.read_bytes()==before
assert run('--update','--only=uniform').returncode==1
assert TABLE.read_bytes()==before
checked=run('--require-rows')
assert checked.returncode==1 and 'CHANGED uniform' in checked.stdout
assert TABLE.read_bytes()==before
(WORK/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
print('PASS read-only observations expose fresh rows; update/check retain mismatch guards; table bytes unchanged')
