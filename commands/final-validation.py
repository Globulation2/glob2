from pathlib import Path
import concurrent.futures,os,subprocess,time
root=Path('/home/bradley/glob2-lean-candidate');e=root/'artifacts/lean-compute/evidence'
while True:
 try:os.kill(874691,0)
 except ProcessLookupError:break
 time.sleep(1)
def run(command,log,env=None):
 with (e/log).open('w') as out:subprocess.run(command,cwd=root,env=dict(os.environ,**(env or {})),stdout=out,stderr=subprocess.STDOUT,check=True)
run(['git','fetch','/tmp/glob2-lean-final-tree.bundle','HEAD'],'final-source-fetch.log');run(['git','checkout','--detach','FETCH_HEAD'],'final-source-checkout.log')
common=dict(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_GRADIENT_BACKEND='cpu')
node='/home/bradley/.nvm/versions/node/v20.19.4/bin:' + os.environ['PATH']
def native():
 run(['python3','tools/dev_build.py','dev_fast=0','release=1','--build=artifacts/lean-compute/native','artifacts/lean-compute/native/src/glob2','engine-tests','unit-tests','-j8'],'final-native-build.log',dict(GLOB2_SDL3_PREFIX='/home/bradley/glob2/build/sdl3/prefix'))
 run(['artifacts/lean-compute/native/test/glob2-unit-tests','--test-suite=ComputeExecutor,GradientPipeline,PathGradient,BuildingGradient*,OpenCLRequiredControl','--no-colors=true'],'final-native-unit.log',common)
 run(['artifacts/lean-compute/native/test/glob2-engine-tests','--test-suite=GradientBackendSwitch,WorldSnapshot,AIOrderScheduler,AIPipeline,AIStateContinuation,LegacyAIState,HiringPipeline,HiringPlanner,TeamStatsPipeline,SettingsGraphics,EntityRandomLifecycle,SharedWorkerLifecycle','--no-colors=true'],'final-native-engine.log',common)
 run(['gdb','-batch','-x','artifacts/lean-compute/evidence/cpu-only-gdb.txt','--args','artifacts/lean-compute/native/test/glob2-engine-tests','--test-suite=GradientBackendSwitch','--test-case=CPU-only lifecycle*','--no-colors=true'],'final-cpu-only-gdb.log',common)
 run(['python3','test/check_parallel_compute.py','artifacts/lean-compute/native/src/glob2-frozen-v3','--baseline','/home/bradley/glob2-lean-baseline/artifacts/lean-compute/native/src/glob2-frozen','--output',str(e/'final-parallel-equivalence')],'final-parallel-equivalence.log',common)
def web():
 run(['scons','target=web','release=1','--build=artifacts/lean-compute/web','web-tests','web-serial','web-threaded','-j4'],'final-web-build.log',dict(PATH=node))
 run(['node','/tmp/glob2-browser-corpus.cjs'],'final-browser-lifecycle-runner.log',dict(PATH=node))
 env=dict(PATH=node,GLOB2_TEST_URL='http://127.0.0.1:8791',GLOB2_TEST_BUILD_DIR=str(root/'artifacts/lean-compute/web'),GLOB2_CHROMIUM_ANGLE='swiftshader')
 run(['browser/node_modules/.bin/playwright','test','browser/tests/determinism.spec.js','--config','browser/playwright.config.js','--project','chromium','--grep','complete per-tick simulation trace|verifies the committed match'],'final-browser-determinism.log',env)
 env.update(GLOB2_WEBGPU_EXPECT_DEVICE='NVIDIA',GLOB2_WEBGPU_CPP='1');env.pop('GLOB2_CHROMIUM_ANGLE')
 run(['browser/node_modules/.bin/playwright','test','browser/tests/gradient-compute.spec.js','--config','browser/playwright.config.js','--project','chromium'],'final-webgpu-hardware.log',env)
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
 futures=[pool.submit(native),pool.submit(web)]
 for f in futures:f.result()
for backend in ['cpu','opencl','vulkan']:
 manifest='/home/bradley/glob2-lean-baseline/artifacts/lean-compute/corpus/windows.json' if backend=='cpu' else str(e/f'{backend}-manifest.json')
 run(['python3','test/benchmark_parallel_compute.py','/home/bradley/glob2-lean-baseline/artifacts/lean-compute/native/src/glob2-frozen','artifacts/lean-compute/native/src/glob2-frozen-v3',manifest,'--verify','--threads','1','2','4','8','--output',str(e/f'final-{backend}-corpus-equivalence')],f'final-{backend}-corpus-equivalence.log',common)
(e/'final-validation-complete.txt').write_text('All final builds, focused native/browser/device, CPU service guard, replay/save continuation and 3-backend corpus comparisons passed.\n')
for backend in ['opencl','vulkan']:run(['python3','/tmp/glob2-gpu-pairs.py',backend],f'{backend}-paired-final-runner.log',common)
