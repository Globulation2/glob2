import json,shlex,subprocess
from pathlib import Path
O=Path(__file__).resolve().parent
logs=sorted(O.parent.glob('cache-build*.log'),key=lambda p:p.stat().st_mtime)
lines=[s for p in logs for s in p.read_text().splitlines() if s.startswith('g++ ')]
commands=[]
for source in ['src/map/editor/MapEditDraw.cpp','src/map/editor/WidgetsTools.cpp']:
 dest=O/('latest-'+Path(source).name)
 dest.write_bytes(subprocess.check_output(['git','show','origin/master:'+source]))
 cmd=shlex.split(next(s for s in reversed(lines) if s.endswith(' '+source) and ' -c ' in s))
 i=cmd.index('-o');del cmd[i:i+2];cmd.remove('-c');cmd[-1]=str(dest);cmd.append('-fsyntax-only')
 commands.append(cmd);subprocess.run(cmd,check=True)
(O/'latest-editor-commands.json').write_text(json.dumps({'base':'0e6f17204','head':'b2850bb8b','commands':commands},indent=2))
print('Both new editor source files compile against the tested PR interfaces')
