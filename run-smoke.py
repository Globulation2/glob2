#!/usr/bin/env python3
from pathlib import Path
import os, resource, shlex, subprocess, sys, tempfile
root=Path(sys.argv[1]).resolve();evidence=Path(__file__).resolve().parent
packages=['sdl2','SDL2_net','SDL2_ttf','SDL2_image','vorbisfile','speex','fribidi','epoxy']
def pkg(option):return shlex.split(subprocess.check_output(['pkg-config',option,*packages],text=True))
flags=['-std=gnu++20','-O1','-UNDEBUG','-I.','-Isrc','-Itest','-Ilibgag/include','-Ilibusl/src',*['-I'+str(p) for p in (root/'src').rglob('*') if p.is_dir()],*pkg('--cflags')]
libs=pkg('--libs')+['-lboost_date_time','-lpthread','-lz']
libs+=['-framework','OpenGL','-framework','GLUT'] if sys.platform=='darwin' else ['-lGL','-lGLU']
sources=(root/'src/SConscript').read_text().split('"""')[1].split()
objects=[root/'build/src'/Path(s).with_suffix('.o') for s in sources if s!='Glob2.cpp']+[root/'build/libgag/src/libgag.a',root/'build/libusl/src/libusl.a']
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='torus-spectator-smoke-') as temp:
 temp=Path(temp);exe=temp/'smoke'
 def link(objs):subprocess.run(['c++',*flags,str(evidence/'SpectatorKeySmoke.cpp'),*map(str,objs),*libs,'-o',str(exe)],cwd=root,check=True)
 def run(name,gpu):
  profile=temp/name;profile.mkdir();env=dict(os.environ,GLOB2_USER_DIR=str(profile),SDL_AUDIODRIVER='dummy')
  if not gpu:env['SDL_VIDEODRIVER']='dummy'
  result=subprocess.run([str(exe),'-g' if gpu else '-G','-F','-m','-s','1120x720'],cwd=root,env=env,capture_output=True,text=True)
  print(name,result.returncode,result.stdout,result.stderr)
  return result
 link(objects)
 assert run('fixed-gpu',True).returncode==0
 assert run('fixed-software',False).returncode==0
 before=temp/'before.cpp';before.write_bytes(subprocess.check_output(['git','show','b28b4333f5cd359ce187460e0f53016232236222:src/gui/GameGUIInputKey.cpp'],cwd=root))
 baseline=temp/'before.o';subprocess.run(['c++',*flags,'-c',str(before),'-o',str(baseline)],cwd=root,check=True)
 link([baseline if p==root/'build/src/gui/GameGUIInputKey.o' else p for p in objects])
 result=run('baseline-gpu',True)
 assert result.returncode!=0 and 'gui.torusView.enabled() == gpu' in result.stderr
