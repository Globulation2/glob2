from pathlib import Path
import subprocess, shlex
folder=Path('artifacts/live-worker'); build=Path('build/darwin/client/release/test')
line=next(x for x in (folder/'build-final-viewer.log').read_text().splitlines() if x.startswith('g++ ') and ' -c ' not in x)
args=shlex.split(line)
args[args.index('-o')+1]=str(folder/'focused/test/glob2-unit-tests')
(folder/'focused/test').mkdir(parents=True,exist_ok=True)
args=[x for x in args if not x.endswith(('SkinPreview.o','LiveWorkerRig.o','LiveWorkerPreview.o'))]
objects=[build/name for name in ['unit-support_TestMain.o','unit-support_Glob2Test.o','unit-support_GlobalContainerSlot.o','unit-support_TestActivity_mac.o','unit-tools_skins_LiveWorkerRig.o','unit-tools_skins_LiveWorkerRigTest.o','unit-libgag_src_SkinMeshTest.o']]
args[1:1]=[str(x) for x in objects]
print(shlex.join(args),flush=True)
subprocess.run(args,check=True)
