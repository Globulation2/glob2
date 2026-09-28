from pathlib import Path
import shlex, subprocess, json
root=Path('artifacts/landscape-performance')
commands=[shlex.split(line) for line in (root/'build-base.log').read_text().splitlines() if line.startswith('g++ ') and '-o build/darwin/client/release/src/CustomGameSetupHarness ' in line]
if not commands:
    raise SystemExit('Link command not yet available')
command=commands[-1]
for i,arg in enumerate(command):
    if arg=='build/darwin/client/release/src/CustomGameSetupHarness':
        command[i]=str(root/'CustomGameSetupHarness-base')
    elif arg.endswith('.o') and (root/'baseline-objects'/Path(arg).name).exists():
        command[i]=str(root/'baseline-objects'/Path(arg).name)
(root/'baseline-link.json').write_text(json.dumps(command,indent=2))
subprocess.run(command,check=True)
