from pathlib import Path
import shlex
import subprocess
root=Path.cwd()
folder=root/'artifacts/live-worker'
main=folder/'RigTestMain.cpp'
main.write_text('#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN\n#include "test/support/Glob2Test.h"\n')
commands=(folder/'build-tests.log').read_text().splitlines()
compile_line=next(x for x in commands if x.startswith('g++ ') and ' -c ' in x and 'unit-tools_skins_LiveWorkerRigTest.o' in x)
args=shlex.split(compile_line)
args[args.index('-o')+1]=str(folder/'RigTestMain.o')
args[-1]=str(main)
print(shlex.join(args),flush=True)
subprocess.run(args,check=True)
link_line=next(x for x in (folder/'build-final-viewer.log').read_text().splitlines() if x.startswith('g++ ') and ' -c ' not in x)
args=shlex.split(link_line)
args[args.index('-o')+1]=str(folder/'rig-tests')
args=[x for x in args if not x.endswith(('SkinPreview.o','LiveWorkerRig.o','LiveWorkerPreview.o'))]
args[1:1]=[str(folder/'RigTestMain.o'),'build/darwin/client/release/test/unit-tools_skins_LiveWorkerRig.o','build/darwin/client/release/test/unit-tools_skins_LiveWorkerRigTest.o']
print(shlex.join(args),flush=True)
subprocess.run(args,check=True)
subprocess.run([str(folder/'rig-tests')],check=True)
