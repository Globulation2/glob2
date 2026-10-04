import sys,json,time
from pathlib import Path
sys.path.insert(0,str(Path('tools/music').resolve()))
from glob2music.manifest import build_set
for name in ['moss-lanterns','thistle-waltz','bramble-jig','fennel-mist','glass-garden','woodland','apple-cider','curious-critters','orchestral-dawn']:
 print('BUILD',name,flush=True)
 try:
  trio,report=build_set(name,offline=True)
  print('RESULT',name,trio.frames,report.failed,flush=True)
 except Exception as e:
  print('ERROR',name,repr(e),flush=True)
