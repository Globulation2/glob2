import os, pathlib, shlex, subprocess
p=pathlib.Path('artifacts/ci-repair/generator-build/test'); p.mkdir(parents=True, exist_ok=True)
r=subprocess.run(['scons','-n','release=1','server=0','tests'],check=True,text=True,capture_output=True)
for line in r.stdout.splitlines():
    args=shlex.split(line)
    if len(args)>3 and args[1]=='-o' and args[2].endswith('/test/glob2-engine-tests'):
        args[2]=str(p/'glob2-engine-tests')
        print(shlex.join(args),flush=True)
        subprocess.run(args,check=True)
        break
else:
    raise SystemExit('No pending engine link command')
