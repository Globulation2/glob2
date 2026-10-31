import os
from pathlib import Path
import subprocess
import sys
codec = sys.argv[1]
root = Path('artifacts/recording-dependencies').resolve()
build = Path('build/linux/client/debug/dev-dev_fast-true-linker-auto')
if codec != 'h264': build /= 'recording-' + codec
output = root / ('official-' + codec)
output.mkdir(parents=True, exist_ok=True)
environment = {**os.environ, 'GLOB2_TEST_FFMPEG':'/usr/bin/ffmpeg', 'GLOB2_TEST_FFPROBE':'/usr/bin/ffprobe', 'GLOB2_RENDERER':'software'}
# The inherited desktop display is unavailable in this shell. Let the test
# runner allocate its own Xvfb for display cases.
for name in ('DISPLAY', 'WAYLAND_DISPLAY'):
 environment.pop(name, None)
selected = sys.argv[2:] or ['unit','engine']
for kind, patterns in [('unit', ['RecordingSession/*','GameplayRecording/*','CommandLine/*']), ('engine', ['GameplayRecording.Integration/*','CommandLine/*'])]:
 if kind not in selected: continue
 if kind == 'engine': environment['SDL_VIDEO_DRIVER'] = 'x11'
 command = ['python3','test/run_tests.py','--build-dir',str(build),'--binary',kind,'-j2','--display-jobs','1','--artifacts',str(output/kind),'--junit',str(output/(kind+'.xml'))]
 for pattern in patterns: command += ['--filter',pattern]
 with (output/(kind+'.log')).open('w') as log:
  result = subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT)
 if result.returncode:
  print((output/(kind+'.log')).read_text()[-12000:]); raise SystemExit(result.returncode)
 print(codec,kind,'passed',flush=True)
