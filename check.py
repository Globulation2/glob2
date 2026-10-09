import subprocess,shlex,sys
from pathlib import Path
line=next(l for l in Path('artifacts/windows-near-repair/commands.log').read_text().splitlines() if l.startswith('g++ ') and l.endswith('src/hud/touch/GameGUITouchHarness.cpp'))
a=shlex.split(line);i=a.index('-o');del a[i:i+2];a.remove('-c');a+=['-fsyntax-only','-Dnear=']
print(shlex.join(a),flush=True)
p=subprocess.run(a);sys.exit(p.returncode)
