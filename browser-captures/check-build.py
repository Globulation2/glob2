from pathlib import Path
import time,json
p=Path('artifacts/bulgarian/browser-release-d3f7.log');s=p.read_text(errors='replace');r={'utc':time.strftime('%H:%M:%S',time.gmtime()),'serial_objects':len(list(Path('build/emscripten/client/release/obj').rglob('*.o'))),'threaded_objects':len(list(Path('build/emscripten/client/release/threaded').rglob('*.o'))),'latest':next((x.rsplit(' ',1)[-1] for x in reversed(s.splitlines()) if x.startswith('/usr/bin/ccache') and '.cpp' in x),None),'errors':[x[:180] for x in s.splitlines() if 'error:' in x or 'scons: ***' in x][-3:],'done':'scons: done building targets.' in s};print(json.dumps(r))
with Path('artifacts/bulgarian/browser-captures/build-progress.jsonl').open('a') as f:f.write(json.dumps(r)+'\n')
