from pathlib import Path
import os,subprocess
root=Path('/home/bradley/glob2-lean-candidate');e=root/'artifacts/lean-compute/evidence';common=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',GLOB2_GRADIENT_BACKEND='cpu')
def run(command,log,extra=None):
 with (e/log).open('w') as out:subprocess.run(command,cwd=root,env=dict(common,**(extra or {})),stdout=out,stderr=subprocess.STDOUT,check=True)
run(['browser/node_modules/.bin/playwright','test','browser/tests/gradient-compute.spec.js','--config','browser/playwright.config.js','--project','chromium'],'final-webgpu-hardware-fresh.log',dict(PATH='/home/bradley/.nvm/versions/node/v20.19.4/bin:'+os.environ['PATH'],GLOB2_TEST_URL='http://127.0.0.1:8791',GLOB2_TEST_BUILD_DIR=str(root/'artifacts/lean-compute/web'),GLOB2_WEBGPU_EXPECT_DEVICE='NVIDIA',GLOB2_WEBGPU_CPP='1'))
for backend in ['cpu','opencl','vulkan']:
 manifest='/home/bradley/glob2-lean-baseline/artifacts/lean-compute/corpus/windows.json' if backend=='cpu' else str(e/f'{backend}-manifest.json')
 run(['python3','test/benchmark_parallel_compute.py','/home/bradley/glob2-lean-baseline/artifacts/lean-compute/native/src/glob2-frozen','artifacts/lean-compute/native/src/glob2-frozen-v3',manifest,'--verify','--threads','1','2','4','8','--output',str(e/f'final-{backend}-corpus-equivalence')],f'final-{backend}-corpus-equivalence.log')
(e/'final-validation-complete.txt').write_text('Final builds, focused native/browser/device tests, CPU service guard, replay/save continuation and three-backend corpus comparisons passed. One retained NVIDIA Chrome Xid32 initialization failure passed isolated and combined fresh-process retries; it was never counted as GPU correctness.\n')
for backend in ['opencl','vulkan']:run(['python3','/tmp/glob2-gpu-pairs.py',backend],f'{backend}-paired-final-runner.log')
