from pathlib import Path
import subprocess,sys,shlex
root=Path.cwd();sys.path.insert(0,str(root/'scons'))
from sources import INCLUDE_DIRECTORIES
flags=['clang++','-std=c++20','-fsyntax-only','-DHAVE_CONFIG_H','-I'+str(root),'-I'+str(root/'libgag/include'),'-I'+str(root/'libusl/src')]
flags+=['-I'+str(root/p) for p in INCLUDE_DIRECTORIES]
flags+=['-I/Users/bradley/glob2','-I/Users/bradley/glob2/build/darwin/client/debug/dev-dev_fast-true-linker-auto/include','-I'+str(root/'third_party/quickjs-ng'),'-I'+str(root/'test/support'),'-I'+str(root/'test/third_party'),'-I/opt/homebrew/opt/sdl3/include']
flags+=shlex.split(subprocess.check_output(['pkg-config','--cflags','sdl3-image','sdl3-net','sdl3-ttf'],text=True))
for name in sys.argv[1:]:
 print('Checking',name,flush=True)
 r=subprocess.run(flags+[name],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 (root/'artifacts/checksum-draft'/ (Path(name).stem+'.syntax.log')).write_text(r.stdout)
 print(r.stdout[-12000:] if r.returncode else 'OK',flush=True)
 if r.returncode:sys.exit(r.returncode)
